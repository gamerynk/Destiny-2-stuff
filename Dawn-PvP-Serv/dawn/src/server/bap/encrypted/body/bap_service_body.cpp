#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <chrono>
#include <limits>

#include "../../../../core/logging/log.h"
#include "../../../../middleware/bap/account_translation/account_translation_response.h"
#include "../../../../middleware/bap/account_translation/request_observation.h"
#include "../../../../middleware/bap/activity_host/activity_host_response.h"
#include "../../../../middleware/bap/certificate.h"
#include "../../../../middleware/bap/client_config/client_config_response.h"
#include "../../../../middleware/bap/family_subscription.h"
#include "../../../../middleware/bap/family_unsubscription.h"
#include "../../../../middleware/bap/user_message/user_message_response.h"
#include "../../../../middleware/encoding/byte_order.h"
#include "../../../../middleware/encoding/bit_reader.h"
#include "../../../../middleware/web_service/messages/opcode501_codec.h"
#include "../../../../middleware/web_service/messages/opcode505/opcode505_codec.h"
#include "../../../../state/runtime/runtime.h"
#include "../../../web_service/web_service_runtime.h"
#include "../../../transport/native_certificate_cache.h"
#include "../../../transport/native_certificate_response.h"
#include "../activity_host_manager/activity_host_manager_route.h"
#include "../activity_message/activity_message_route.h"
#include "../internal.h"
#include "../matchmaking/matchmaking_route.h"
#include "../queuez/queuez_state_validation.h"

namespace dawn::server::bap::encrypted::body {
namespace {

constexpr std::size_t kSubscribeReportLimit = 96;
constexpr std::size_t kTranslationReportLimit = 384;
constexpr std::uint16_t kCharacterDeleteOpcode = 502;

} // namespace

bool process(const ServiceRoute& route,
             const queuez::SessionState& queuezState,
             std::uint64_t activitySessionId,
             state::matchmaking::ContextHandle matchmakingContext,
             std::uint64_t accountSoid,
             std::uint64_t platformId,
             std::span<const std::byte> requestBody,
             std::span<std::byte> output,
             std::size_t& written,
             ServiceOutcome& outcome,
             std::uint64_t activityJoinRevision,
             const middleware::bap::activity_message::patch_epoch::PatchEpoch* expectedEpoch,
             const middleware::bap::activity_message::RequestProvenance& provenance) noexcept {
    reset(outcome);
    switch (route.bodyCodec) {
    case BodyCodec::empty:
        // capture round: the clan body layout is undocumented, so log each svc-44 request raw
        if (route.response == middleware::bap::ResponseService::clan) {
            constexpr std::size_t kChunk = 96;
            constexpr std::size_t kCaptureLimit = 2048;
            const std::size_t total = (std::min)(requestBody.size(), kCaptureLimit);
            for (std::size_t offset = 0; offset < total || offset == 0; offset += kChunk) {
                std::array<char, 320> line{};
                int count = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=clan stage=capture body_bytes=%zu offset=%zu hex=",
                                          requestBody.size(),
                                          offset);
                for (std::size_t index = offset; count > 0 && index < total && index < offset + kChunk;
                     ++index) {
                    count += std::snprintf(line.data() + count,
                                           line.size() - static_cast<std::size_t>(count),
                                           "%02X",
                                           std::to_integer<unsigned>(requestBody[index]));
                }
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::info,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
                if (total == 0) {
                    break;
                }
            }
        }
        written = 0;
        return true;
    case BodyCodec::accountTranslationResponse: {
        struct Resolution {
            std::uint64_t platform;
            std::uint64_t account;
            unsigned local{};
            unsigned remote{};
            unsigned missing{};
        } resolution{platformId, accountSoid};
        const auto resolve = +[](void* context, std::uint64_t requested) noexcept {
            auto& state = *static_cast<Resolution*>(context);
            if (requested == state.platform) {
                ++state.local;
                return state.account;
            }
            std::uint64_t account = 0;
            if (dawn::state::resolve_friend_account(state.platform, requested, account)) {
                ++state.remote;
                return account;
            }
            ++state.missing;
            return std::uint64_t{0};
        };
        const auto observed = middleware::bap::account_translation::observe_request(requestBody);
        const bool encoded = middleware::bap::account_translation::encode_response(
            requestBody, &resolution, resolve, output, written);
        std::array<char, kTranslationReportLimit> line{};
        const int count = std::snprintf(
            line.data(), line.size(),
            "ev=queuez stage=translate result=%s viewer=0x%016llX count=%u "
            "local=%u remote=%u unpaired=%u request_bytes=%zu response_bytes=%zu "
            "first_identity=0x%016llX parse_reason=%s",
            encoded ? "encoded" : "capacity", static_cast<unsigned long long>(platformId),
            static_cast<unsigned>(observed.count), resolution.local, resolution.remote,
            resolution.missing, requestBody.size(), written,
            static_cast<unsigned long long>(observed.firstIdentity), observed.reason);
        if (count > 0) {
            core::log::write(core::log::Channel::server, core::log::Level::info,
                {line.data(), (std::min)(static_cast<std::size_t>(count), line.size() - 1)});
        }
        return encoded;
    }
    case BodyCodec::activityHostManagerResponse: {
        state::activity::PendingAllocation allocation{};
        bool hasAllocation = false;
        const bool encoded = activity_host_manager::encode_response(
            requestBody, output, written, allocation, hasAllocation);
        if (encoded && hasAllocation) {
            outcome.transaction = allocation;
        }
        return encoded;
    }
    case BodyCodec::activityMessageRequest: {
        written = 0;
        activity_message::ActivityPlan plan{};
        bool hasTransaction = false;
        const bool processed =
            activity_message::process(activitySessionId, requestBody, plan, hasTransaction,activityJoinRevision,expectedEpoch,provenance);
        if (processed && hasTransaction) {
            outcome.transaction = plan;
        }
        return processed;
    }
    case BodyCodec::activityHostResponse: {
        const state::SignOnState& signOn = state::sign_on();
        return middleware::bap::activity_host::encode_response(
            requestBody, signOn.relayAddress, signOn.relayPort, output, written);
    }
    case BodyCodec::clientConfigResponse:
        return middleware::bap::client_config::encode_minimal_response(output, written);
    case BodyCodec::familySubscription: {
        written = 0;
        outcome.hasSubscription =
            middleware::bap::family_subscription::parse(requestBody, outcome.subscription);
        std::array<char, kSubscribeReportLimit> line{};
        const int count =
            std::snprintf(line.data(),
                          line.size(),
                          "ev=queuez stage=subscribe result=%s family=%u root=0x%016llX",
                          outcome.hasSubscription ? "ok" : "unreadable",
                          static_cast<unsigned>(outcome.subscription.familyType),
                          static_cast<unsigned long long>(outcome.subscription.familyRootSoid));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return outcome.hasSubscription;
    }
    case BodyCodec::familyUnsubscription: {
        written = 0;
        outcome.hasUnsubscription =
            middleware::bap::family_unsubscription::parse(requestBody, outcome.unsubscription);
        if (outcome.hasUnsubscription) {
            std::array<char, kSubscribeReportLimit> line{};
            const int count = std::snprintf(
                line.data(),
                line.size(),
                "ev=queuez stage=unsubscribe result=ok family=%u root=0x%016llX",
                static_cast<unsigned>(outcome.unsubscription.familyType),
                static_cast<unsigned long long>(outcome.unsubscription.familyRootSoid));
            if (count > 0) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::info,
                                 {line.data(), static_cast<std::size_t>(count)});
            }
        }
        return outcome.hasUnsubscription;
    }
    case BodyCodec::matchmakingResponse: {
        state::matchmaking::PendingMutation mutation{};
        bool hasMutation = false;
        const bool encoded = matchmaking::encode_response(
            matchmakingContext, requestBody, output, written, mutation, hasMutation);
        if (encoded && hasMutation) {
            outcome.transaction = mutation;
        }
        return encoded;
    }
    case BodyCodec::steamCertificate:
    {
        (void)transport::native_certificate_cache::initialize_storage();
        transport::native_certificate_cache::Snapshot certificate{};
        const char* reason = "invalid_time";
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const bool matched = now > 0 && transport::native_certificate_cache::find(
            platformId, requestBody, static_cast<std::uint64_t>(now), certificate, &reason);
        std::array<char, 192> line{};
        const int length = std::snprintf(line.data(), line.size(),
            "ev=bap stage=certificate source=%s key_match=%u reason=%s",
            matched ? "valve_signed_cache" : "native_refusal", static_cast<unsigned>(matched), reason);
        if (length > 0) core::log::write(core::log::Channel::server, core::log::Level::info,
            {line.data(), static_cast<std::size_t>(length)});
        outcome.responseStatus = transport::native_certificate_response::encode(
            requestBody, matched, certificate, output, written);
        return true;
    }
    case BodyCodec::userMessageResponse:
        return middleware::bap::user_message::encode_minimal_response(output, written);
    case BodyCodec::webService: {
        middleware::web_service::Message message;
        const bool parsed = middleware::web_service::parse_request(requestBody, message);
        if (parsed && message.opcode == kCharacterDeleteOpcode) {
            // delete character: one bare 64-bit id; the reply is a status pair whose value (+4) is the family-4 version the client awaits
            // a generic echo is the wrong shape; the store's delete arms a resync that publishes version + 1
            std::uint64_t characterSoid = 0;
            bool readSoid = false;
            if (message.payload.size() >= sizeof characterSoid) {
                middleware::encoding::bits::Reader reader(message.payload);
                readSoid = reader.read(64, characterSoid) && characterSoid != 0;
            }
            const bool versioned =
                queuezState.family4Active
                && queuezState.family4Version < (std::numeric_limits<std::int32_t>::max)();
            const bool deleted = readSoid && versioned && state::delete_character(characterSoid);
            middleware::web_service::StatusResponse status{};
            if (deleted) {
                status.value = queuezState.family4Version + 1;
            } else {
                status.code = 1;
            }
            std::array<char, 192> line{};
            const int count = std::snprintf(
                line.data(),
                line.size(),
                "ev=ws502 stage=delete result=%s soid=0x%llX read=%u family4=%u version=%d",
                deleted ? "ok" : "fail",
                static_cast<unsigned long long>(characterSoid),
                readSoid ? 1U : 0U,
                versioned ? 1U : 0U,
                deleted ? status.value : -1);
            if (count > 0) {
                core::log::write(core::log::Channel::server,
                                 deleted ? core::log::Level::info : core::log::Level::warn,
                                 {line.data(),
                                  (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
            }
            return middleware::web_service::encode_response(
                message,
                middleware::web_service::ResponseShape::statusPair,
                status,
                output,
                written);
        }
        if (parsed
            && message.opcode == middleware::web_service::messages::opcode501::kOpcode) {
            // a create is a SELECT move: the new character becomes the family-4 resident and the client awaits that version
            namespace op501 = middleware::web_service::messages::opcode501;
            op501::Request decoded{};
            state::CharacterCreateRequest create{};
            std::uint64_t createdSoid = 0;
            bool selectionChanged = false;
            const bool parsedCreate = op501::parse_request(message, decoded);
            if (parsedCreate) {
                create.accountSoid = accountSoid;
                create.characterClass = decoded.characterClass;
                create.race = decoded.race;
                create.gender = decoded.gender;
                create.identityHeader = decoded.identityHeader;
                create.hasIdentityHeader = true;
            }
            const bool created = parsedCreate && state::create_character(create, createdSoid);
            const bool selected =
                created && state::set_selected_character(createdSoid, selectionChanged);
            const bool staged =
                selected
                && queuez::stage_select_character(queuezState, createdSoid, outcome.selectCharacter)
                && op501::encode_response(message,
                                          outcome.selectCharacter.after.family4Version,
                                          createdSoid,
                                          output,
                                          written);
            std::array<char, 256> line{};
            const int count = std::snprintf(
                line.data(),
                line.size(),
                "ev=ws501 stage=create result=%s parsed=%u created=%u selected=%u class=%u race=%u "
                "gender=%u soid=0x%llX version=%d bytes=%zu",
                staged ? "ok" : "fail",
                parsedCreate ? 1U : 0U,
                created ? 1U : 0U,
                selected ? 1U : 0U,
                static_cast<unsigned>(decoded.characterClass),
                static_cast<unsigned>(decoded.race),
                static_cast<unsigned>(decoded.gender),
                static_cast<unsigned long long>(createdSoid),
                staged ? static_cast<int>(outcome.selectCharacter.after.family4Version) : -1,
                message.payload.size());
            if (count > 0) {
                core::log::write(core::log::Channel::server,
                                 staged ? core::log::Level::info : core::log::Level::warn,
                                 {line.data(),
                                  (std::min)(static_cast<std::size_t>(count), line.size() - 1U)});
            }
            if (staged) {
                outcome.hasSelectCharacter = true;
                return true;
            }
            // a refused status still answers the transaction so the creation screen doesn't hang
            outcome.selectCharacter = {};
            middleware::web_service::StatusResponse refused{};
            refused.code = 1;
            return middleware::web_service::encode_response(
                message,
                middleware::web_service::ResponseShape::statusPair,
                refused,
                output,
                written);
        }
        if (parsed
            && message.opcode == middleware::web_service::messages::opcode505::kOpcode) {
            if (!middleware::web_service::messages::opcode505::parse_request(message)
                || !queuez::stage_change_character(queuezState, outcome.changeCharacter)
                || !middleware::web_service::messages::opcode505::encode_response(
                    message, outcome.changeCharacter.after.family4Version, output, written)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=ws505 stage=change result=fail");
                // the plain status pair still goes out; Change Character waits on the echoed transaction id and hangs without it
                outcome.changeCharacter = {};
                return middleware::web_service::encode_response(
                    message,
                    middleware::web_service::ResponseShape::statusPair,
                    middleware::web_service::StatusResponse{},
                    output,
                    written);
            }
            outcome.hasChangeCharacter = true;
            return true;
        }
        thread_local web_service::Outcome webOutcome;
        if (!dawn::server::web_service::consume(requestBody, output, written, webOutcome)) {
            return false;
        }
        outcome.hasSubscription = webOutcome.hasSubscription;
        outcome.subscription = webOutcome.subscription;
        const auto* equipmentSwap =
            web_service::mutation_if<state::PendingEquipmentSwap>(webOutcome);
        const auto* socketPlug = web_service::mutation_if<state::PendingSocketPlug>(webOutcome);
        const auto* itemState = web_service::mutation_if<state::PendingItemState>(webOutcome);
        const auto* itemAcquisition =
            web_service::mutation_if<state::PendingItemAcquisition>(webOutcome);
        const auto* profileItemAcquisition =
            web_service::mutation_if<state::PendingProfileItemAcquisition>(webOutcome);
        const auto* itemDismantle =
            web_service::mutation_if<state::PendingItemDismantle>(webOutcome);
        const auto* vaultTransfer =
            web_service::mutation_if<state::PendingVaultTransfer>(webOutcome);
        const auto* abilitySelection =
            web_service::mutation_if<state::PendingAbilitySelection>(webOutcome);
        if (equipmentSwap != nullptr) {
            // equip is optimistic; its status-pair value is the Family-4 revision whose Queuez frame makes it authoritative, so stage it before encoding the reply
            auto& transaction = outcome.transaction.emplace<EquipmentSwapTransaction>();
            if (!queuez::stage_equipment_swap(
                    queuezState, equipmentSwap->characterSoid, transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=ws403 stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=ws403 stage=response result=fail");
                    return false;
                }
                web_service::report_equip_response(message, status.value, output.first(written));
                transaction.pending = *equipmentSwap;
            }
        }
        if (socketPlug != nullptr) {
            auto& transaction = outcome.transaction.emplace<SocketPlugTransaction>();
            if (!queuez::stage_socket_plug(queuezState,
                                           socketPlug->accountSoid,
                                           socketPlug->characterSoid,
                                           socketPlug->targetInstanceSoid,
                                           socketPlug->profileChanged,
                                           transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=socket_plug stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=socket_plug stage=response result=fail");
                    return false;
                }
                web_service::report_socket_plug_response(message,
                                                         status.value,
                                                         socketPlug->targetInstanceSoid,
                                                         socketPlug->socketLane,
                                                         socketPlug->plugDefinitionIndex,
                                                         output.first(written));
                transaction.pending = *socketPlug;
            }
        }
        if (itemState != nullptr) {
            auto& transaction = outcome.transaction.emplace<ItemStateTransaction>();
            if (!queuez::stage_equipment_swap(
                    queuezState, itemState->characterSoid, transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=item_state stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=item_state stage=response result=fail");
                    return false;
                }
                transaction.pending = *itemState;
            }
        }
        if (itemAcquisition != nullptr) {
            auto& transaction = outcome.transaction.emplace<ItemAcquisitionTransaction>();
            if (!queuez::stage_item_acquisition(queuezState,
                                                itemAcquisition->accountSoid,
                                                itemAcquisition->characterSoid,
                                                itemAcquisition->acquiredInstanceSoid,
                                                itemAcquisition->profileChanged,
                                                transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=acquire stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=acquire stage=response result=fail");
                    return false;
                }
                web_service::report_item_acquisition_response(message,
                                                              status.value,
                                                              itemAcquisition->acquiredInstanceSoid,
                                                              output.first(written));
                transaction.pending = *itemAcquisition;
            }
        }
        if (profileItemAcquisition != nullptr) {
            auto& transaction = outcome.transaction.emplace<ProfileItemAcquisitionTransaction>();
            if (!queuez::stage_profile_item_acquisition(
                    queuezState,
                    profileItemAcquisition->accountSoid,
                    profileItemAcquisition->acquiredInstanceSoid,
                    profileItemAcquisition->actionSource,
                    profileItemAcquisition->appended,
                    transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=profile_acquire stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=profile_acquire stage=response result=fail");
                    return false;
                }
                web_service::report_profile_item_acquisition_response(
                    message,
                    status.value,
                    profileItemAcquisition->acquiredDefinitionHash,
                    profileItemAcquisition->acquiredQuantity,
                    output.first(written));
                transaction.pending = *profileItemAcquisition;
            }
        }
        if (itemDismantle != nullptr) {
            auto& transaction = outcome.transaction.emplace<ItemDismantleTransaction>();
            if (!queuez::stage_item_dismantle(queuezState,
                                              itemDismantle->accountSoid,
                                              itemDismantle->characterSoid,
                                              itemDismantle->dismantledInstanceSoid,
                                              itemDismantle->profileChanged,
                                              transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=dismantle stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=dismantle stage=response result=fail");
                    return false;
                }
                web_service::report_item_dismantle_response(message,
                                                            status.value,
                                                            itemDismantle->dismantledInstanceSoid,
                                                            output.first(written));
                transaction.pending = *itemDismantle;
            }
        }
        if (vaultTransfer != nullptr) {
            auto& transaction = outcome.transaction.emplace<VaultTransferTransaction>();
            if (!queuez::stage_vault_transfer(queuezState,
                                              vaultTransfer->accountSoid,
                                              vaultTransfer->characterSoid,
                                              vaultTransfer->instanceSoid,
                                              transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=vault stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=vault stage=response result=fail");
                    return false;
                }
                transaction.pending = *vaultTransfer;
            }
        }
        if (abilitySelection != nullptr) {
            auto& transaction = outcome.transaction.emplace<AbilitySelectionTransaction>();
            if (!queuez::stage_equipment_swap(
                    queuezState, abilitySelection->characterSoid, transaction.update)) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::warn,
                                 "ev=ability stage=queuez_preflight result=fail");
                outcome.transaction = std::monostate{};
            } else {
                middleware::web_service::StatusResponse status{};
                status.value = transaction.update.after.family4Version;
                if (!middleware::web_service::encode_response(
                        message,
                        middleware::web_service::ResponseShape::statusPair,
                        status,
                        output,
                        written)) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::warn,
                                     "ev=ability stage=response result=fail");
                    return false;
                }
                transaction.pending = *abilitySelection;
            }
        }
        // a same-character pick stages an in-place resident refresh so orbit is built from the current record without releasing the key
        if (webOutcome.hasSelectedCharacter
            && queuez::stage_select_character(
                queuezState, webOutcome.selectedCharacterSoid, outcome.selectCharacter)) {
            outcome.hasSelectCharacter = true;
        } else {
            outcome.selectCharacter = {};
        }
        return true;
    }
    }
    written = 0;
    return false;
}

} // namespace dawn::server::bap::encrypted::body
