#include "activity_message_route.h"
#include "../../../../core/runtime/runtime_policy.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <Windows.h>

#include "../../../../core/logging/log.h"
#include "../../../../core/settings/settings.h"
#include "../../../../middleware/bap/activity_message/activity_client_identity_parser.h"
#include "../../../../middleware/bap/activity_message/activity_client_keepalive_validator.h"
#include "../../../../middleware/bap/activity_message/activity_high_water_validator.h"
#include "../../../../middleware/bap/activity_message/activity_join_request_parser.h"
#include "../../../../middleware/bap/activity_message/activity_membership_acknowledgement_parser.h"
#include "../../../../middleware/bap/activity_message/activity_message_request_parser.h"
#include "../../../../middleware/bap/activity_message/activity_player_assignment.h"
#include "../../../../middleware/bap/activity_message/activity_state_refresh_parser.h"
#include "../../../../middleware/bap/activity_message/client_authoritative_data.h"
#include "../../../../middleware/bap/activity_message/entity_authority.h"
#include "../../../../middleware/bap/activity_message/entity_slots.h"
#include "../../../../middleware/bap/activity_message/incident.h"
#include "../../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../../state/activity/crucible/native_maps.h"
#include "../../../../state/activity/crucible/native_modes.h"
#include "../../../../state/activity/crucible/match_runtime.h"
#include "../../../../state/activity/runtime.h"
#include "../../../../state/activity/shared/runtime.h"
#include "../../../../state/activity/shared/public_admission.h"
#include "../../../../state/activity/shared/social_admission.h"
#include "../../../../state/runtime/runtime.h"
#include "../../../gameplay/group/group_host_sessions.h"
#include "membership/activity_membership_route.h"
#include "mission_sense_route.h"
#include "middleware/bap/activity_message/activity_entity_slot_request_parser.h"
#include "patch_epoch/activity_patch_epoch_route.h"

namespace dawn::server::bap::encrypted::activity_message {
namespace {
namespace service = middleware::bap::activity_message;
namespace authority = service::entity_authority;
namespace client_keepalive = service::client_keepalive;
namespace high_water = service::high_water;
namespace epoch_message = service::patch_epoch;

constexpr std::uint32_t kJoinRequestMessageType = 3;
constexpr std::uint32_t kSenseUpdateMessageType = 6;
// wide enough for a private match's options (a 1024-char line fits ~450)
constexpr std::size_t kActivityStartPreviewBytes = 400;

struct AcceptedMessage {
    std::uint32_t type;
    const char* name;
};

constexpr std::array<AcceptedMessage, 12> kAcceptedMessages{{
    {6, "sensor_sense_update"},
    {8, "request_activity_host"},
    {11, "start_new_activity"},
    {15, "peer_leave_request"},
    {34, "process_debug_command"},
    {37, "connectivity_failure"},
    {39, "send_client_heartbeat"},
    {43, "bug_claw"},
    {46, "report_lag_switch"},
    {47, "connection_quality_report"},
    {48, "speculative_migration"},
    {50, "refresh_inspirations"},
}};

[[nodiscard]] const char* accepted_name(std::uint32_t messageType) noexcept {
    const auto row = std::find_if(kAcceptedMessages.begin(),
                                  kAcceptedMessages.end(),
                                  [messageType](const AcceptedMessage& candidate) noexcept {
                                      return candidate.type == messageType;
                                  });
    return row == kAcceptedMessages.end() ? nullptr : row->name;
}

void report_accepted(std::uint32_t messageType,
                     const char* name,
                     std::span<const std::byte> payload) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    int written = std::snprintf(line.data(),
                                line.size(),
                                "ev=activity stage=message result=accept type=%u name=%s "
                                "bytes=%zu",
                                messageType,
                                name,
                                payload.size());
    if (written <= 0 || static_cast<std::size_t>(written) >= line.size()) {
        return;
    }
    const bool activityStart = messageType == 8 || messageType == 11;
    if (activityStart) {
        const std::size_t preview =
            (std::min)(payload.size(), kActivityStartPreviewBytes);
        int appended = std::snprintf(line.data() + written,
                                     line.size() - static_cast<std::size_t>(written),
                                     " preview=");
        if (appended <= 0
            || static_cast<std::size_t>(appended)
                   >= line.size() - static_cast<std::size_t>(written)) {
            return;
        }
        written += appended;
        constexpr char kHex[] = "0123456789ABCDEF";
        for (std::size_t index = 0;
             index < preview && static_cast<std::size_t>(written) + 2 < line.size();
            ++index) {
            const unsigned value = std::to_integer<unsigned char>(payload[index]);
            line[static_cast<std::size_t>(written)] = kHex[value >> 4U];
            ++written;
            line[static_cast<std::size_t>(written)] = kHex[value & 0xFU];
            ++written;
        }
    }
    core::log::write(core::log::Channel::server,
                     activityStart ? core::log::Level::info : core::log::Level::debug,
                     {line.data(), static_cast<std::size_t>(written)});
}

// nothing relays msg 19 yet; a failure is named because a bad target index would crash the client if sent on
void report_incident(const service::Request& request) noexcept {
    namespace incident = service::incident;
    incident::Incident parsed;
    const incident::Verdict verdict = incident::validate(request.payload, parsed);
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=incident result=%s target=%u extra=%u "
                                      "selector=%u payload=%u",
                                      incident::verdict_name(verdict),
                                      parsed.primaryTarget,
                                      parsed.extraTargetCount,
                                      static_cast<unsigned>(parsed.hasCompressedSelector),
                                      parsed.payloadLength);
    if (written <= 0) {
        return;
    }
    const auto level =
        verdict == incident::Verdict::accepted ? core::log::Level::debug : core::log::Level::warn;
    core::log::write(
        core::log::Channel::server, level, {line.data(), static_cast<std::size_t>(written)});
}

void report_crucible_incident(std::uint64_t boundSessionId,
                              const service::Request& request) noexcept {
    if (!core::log::accepts(core::log::Channel::server, core::log::Level::debug)
        || boundSessionId == 0 || request.accountHandle != boundSessionId
        || !state::activity::is_joined(boundSessionId)) return;
    state::activity::destination::DestinationSelection selection{};
    if (!state::activity::destination::snapshot(boundSessionId, selection)) return;
    const std::string_view destination{
        reinterpret_cast<const char*>(selection.packageName.data()), selection.packageNameLength};
    const auto* map = state::activity::crucible::find_native_map(destination);
    if (map == nullptr) return;
    const auto* mode = state::activity::crucible::find_native_mode(selection.previousActivityIndex);
    service::incident::Incident parsed{};
    const auto verdict = service::incident::validate(request.payload, parsed);
    if (verdict != service::incident::Verdict::accepted) return;
    constexpr std::size_t kCaptureBytes = 768;
    constexpr std::size_t kChunkBytes = 192;
    const auto captured = request.payload.first((std::min)(request.payload.size(), kCaptureBytes));
    std::uint64_t digest = 14695981039346656037ULL;
    for (auto value : captured) digest = (digest ^ std::to_integer<unsigned char>(value))
                                         * 1099511628211ULL;
    for (std::size_t offset = 0; offset < captured.size(); offset += kChunkBytes) {
        std::array<char, core::log::kLineCapacity> line{};
        const int written = std::snprintf(line.data(), line.size(),
            "ev=pvp stage=incident_wire session=0x%llX account=0x%llX map=%.*s "
            "mode_index=%d target=%u selector=%u bytes=%zu captured=%zu digest=%016llX "
            "offset=%zu wire=",
            static_cast<unsigned long long>(boundSessionId),
            static_cast<unsigned long long>(state::account_snapshot().primarySoid),
            static_cast<int>(destination.size()), destination.data(),
            mode != nullptr ? mode->activityIndex : -1, parsed.primaryTarget,
            parsed.hasCompressedSelector ? 1U : 0U, request.payload.size(), captured.size(),
            static_cast<unsigned long long>(digest), offset);
        if (written <= 0 || static_cast<std::size_t>(written) >= line.size()) return;
        std::size_t length = static_cast<std::size_t>(written);
        const auto chunk = captured.subspan(offset, (std::min)(kChunkBytes, captured.size() - offset));
        if (!core::log::append_hex(line, length, chunk)) return;
        core::log::write(core::log::Channel::server, core::log::Level::debug,
                         {line.data(), length});
    }
}

void report_message(std::uint32_t messageType,
                    std::uint64_t accountHandle,
                    const char* reason) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=message result=skip type=%u "
                                      "handle=0x%llX reason=%s",
                                      messageType,
                                      static_cast<unsigned long long>(accountHandle),
                                      reason);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool prepare_join(const service::Request& request, ActivityPlan& plan) noexcept {
    service::JoinRequest parsed;
    // the client takes the low slots and the server keeps the reserve above them
    const std::size_t reserve =
        core::settings::server::gameplay::effective_reserve(core::settings::get().server.gameplay);
    const std::size_t granted = state::activity::entity_slots::kSlotCount - reserve;
    if (!service::join_request::parse_join_request(request.payload, parsed)
        || parsed.sessionId != request.accountHandle) {
        return false;
    }

    // a derived PUBLIC client can put its platform account in the join member lane, but retail keys public membership with its PRIVATE parent's machine member
    // resolve that parent through the host allocation; ordinary private joins keep their parsed member
    const std::uint64_t sourceSessionId =
        gameplay::group::source_activity_session_for_host(parsed.sessionId);
    const std::uint64_t inheritedMemberKey =
        sourceSessionId != state::activity::kAbsentSessionId
            ? state::activity::membership::member_key(sourceSessionId)
            : 0;
    const std::uint64_t memberKey = inheritedMemberKey != 0 ? inheritedMemberKey : parsed.memberKey;
    std::uint64_t characterSoid=parsed.characterSoid;
    if (sourceSessionId!=0) {
        std::uint64_t caller{};
        const auto context=state::capture_account_runtime(caller);
        state::activity::SessionRecord parent{};
        if (state::activity::snapshot_for_runtime(context,caller,sourceSessionId,parent)
            && parent.joined && parent.membership.hasIdentity)
            characterSoid=parent.membership.identity.opaqueSoid;
    }
    state::AccountRuntimeContext publicOwner{};
    const bool socialAdmission=state::activity::shared::social_join_authorized(parsed.sessionId,memberKey,characterSoid);
    const bool publicAdmission=socialAdmission || (sourceSessionId!=0
        && state::activity::shared::public_join_authorized(parsed.sessionId,memberKey,characterSoid));
    const bool foreignPublic=publicAdmission && state::activity::shared::owner_context(parsed.sessionId,publicOwner)
        && publicOwner.account && publicOwner.account->primarySoid!=state::account_snapshot().primarySoid;
    // a persisted dormant local ID isn't this foreign PUBLIC allocator; import the admitted world instead of reopening the old record
    if (foreignPublic || !state::activity::entity_slots::prepare_join(
            parsed.sessionId, memberKey, granted, reserve, plan.entitySlotMutation)) {
        state::AccountRuntimeContext owner{};
        if (!state::activity::shared::owner_context(parsed.sessionId,owner)) {
            report_message(kJoinRequestMessageType,parsed.sessionId,"shared_owner_missing_or_ambiguous");
            return false;
        }
        const auto caller=state::account_snapshot().primarySoid;
        service::client_identity::ClientIdentity peer{};
        peer.memberKey=memberKey; peer.accountSoid=caller; peer.field5=characterSoid;
        state::activity::membership::Identity privateIdentity{};
        bool directPrivate=false;
        if(!publicAdmission) {
            const state::ScopedAccountRuntime ownerScope(owner);
            if (!membership::authorize_reservations(std::span(&peer,1),parsed.sessionId)) {
                report_message(kJoinRequestMessageType,parsed.sessionId,"shared_current_lobby_denied");
                return false;
            }
            // a late fireteam member can join a running PRIVATE activity with no startup reservation; use the full native type3 identity
            service::client_identity::ClientIdentity full{};
            if(sourceSessionId==0 && request.payload.size()>=12
                && service::client_identity::parse_client_identity(request.payload.subspan(12),full)
                && full.accountSoid==caller && full.memberKey==memberKey && full.field5==characterSoid) {
                privateIdentity={full.memberKey,full.field1,full.field2,full.field3,
                    full.accountSoid,full.field5,full.field6};
                directPrivate=true;
            }
        }
        state::activity::shared::JoinFailure failure{};
        bool sharedPrepared=state::activity::shared::prepare_join(owner,parsed.sessionId,memberKey,
                characterSoid,granted,reserve,plan.entitySlotMutation,&failure);
        if(!sharedPrepared && directPrivate)
            sharedPrepared=state::activity::shared::prepare_join(owner,parsed.sessionId,memberKey,
                characterSoid,granted,reserve,plan.entitySlotMutation,&failure,&privateIdentity);
        if (!sharedPrepared) {
            const char* reason="shared_validation_denied";
            using Failure=state::activity::shared::JoinFailure;
            switch (failure) {
            case Failure::capacity: reason="shared_session_capacity_no_dormant_slot"; break;
            case Failure::reservation: reason="shared_reservation_denied"; break;
            case Failure::reserve: reason="shared_server_reserve_mismatch"; break;
            case Failure::lease: reason="shared_empty_lease"; break;
            default: break;
            }
            report_message(kJoinRequestMessageType,parsed.sessionId,reason);
            return false;
        }
        core::log::write(core::log::Channel::server,core::log::Level::info,
            socialAdmission?"ev=activity stage=shared_join result=prepared kind=social_participant"
            :sourceSessionId==0?"ev=activity stage=shared_join result=prepared kind=private_follower"
                              :"ev=activity stage=shared_join result=prepared kind=public_participant");
    }
    plan.correlation = parsed.correlation;
    plan.entitySlotMutation.peerConnection=parsed.peerConnection;
    plan.entitySlotMutation.peerConnectionGuard=parsed.peerConnection;
    plan.sessionId = parsed.sessionId;
    state::activity::destination::DestinationSelection retirementDestination{};
    const bool hasRetirementDestination = plan.entitySlotMutation.importsShared
        ? (retirementDestination=plan.entitySlotMutation.sharedRecord.destination, true)
        : state::activity::destination::snapshot(parsed.sessionId, retirementDestination);
    const bool competitiveRetirement = hasRetirementDestination
        && service::player_assignment::competitive_destination(std::string_view(
            reinterpret_cast<const char*>(retirementDestination.packageName.data()),
            retirementDestination.packageNameLength));
    if ((sourceSessionId!=0 || socialAdmission || competitiveRetirement)
        && !state::activity::entity_retirement::prepare_join(
            parsed.sessionId,state::account_snapshot().primarySoid,memberKey,
            plan.entitySlotMutation.mask,plan.entitySlotMutation.retirement)) {
        report_message(kJoinRequestMessageType,parsed.sessionId,"retirement_history_or_lease");
        return false;
    }
    plan.joinCharacterSoid = characterSoid;
    plan.delivery = Delivery::joinNotifications;
    plan.mutationDomain = MutationDomain::entitySlots;
    return true;
}

[[nodiscard]] bool prepare_grant(const service::Request& request, ActivityPlan& plan) noexcept {
    std::int32_t requested = 0;
    if (!service::entity_slot_request::parse_entity_slot_request(request.payload, requested)
        || requested <= 0
        || !state::activity::entity_slots::prepare_grant(
            request.accountHandle, static_cast<std::size_t>(requested), plan.entitySlotMutation)) {
        return false;
    }
    static std::atomic<unsigned> grantDiagnostics{};
    if (grantDiagnostics.fetch_add(1, std::memory_order_relaxed) < 32) {
        const auto count = [](const auto& mask) noexcept {
            std::size_t total = 0;
            for (const auto value : mask) total += std::popcount(std::to_integer<unsigned>(value));
            return total;
        };
        std::uint64_t accountSoid = 0;
        const auto context = state::capture_account_runtime(accountSoid);
        state::activity::SessionRecord record{};
        const bool found = state::activity::snapshot_for_runtime(
            context, accountSoid, request.accountHandle, record);
        const auto reserved = count(record.serverEntitySlots);
        const auto excluded = state::activity::shared::excluded_slots(record, reserved);
        std::array<char, core::log::kLineCapacity> line{};
        const int size = std::snprintf(line.data(), line.size(),
            "ev=activity stage=slot_grant session=0x%llX account=0x%llX requested=%d "
            "granted=%zu snapshot=%u held=%zu reserved=%zu legacy_partition_excluded=%zu member=%u parts=%u public=%u joined=%u",
            static_cast<unsigned long long>(request.accountHandle),
            static_cast<unsigned long long>(accountSoid), requested,
            count(plan.entitySlotMutation.mask), static_cast<unsigned>(found),
            count(record.heldEntitySlots), reserved, count(excluded),
            static_cast<unsigned>(record.sharedMemberSlot), static_cast<unsigned>(record.sharedLeaseParts),
            static_cast<unsigned>(record.publicWorld), static_cast<unsigned>(record.joined));
        if (size > 0) core::log::write(core::log::Channel::server, core::log::Level::info,
            {line.data(), (std::min)(static_cast<std::size_t>(size), line.size() - 1)});
    }
    plan.sessionId = request.accountHandle;
    plan.delivery = Delivery::entitySlotNotification;
    plan.mutationDomain = MutationDomain::entitySlots;
    return true;
}

[[nodiscard]] std::size_t
mask_slot_count(const service::entity_slots::EntitySlotMask& mask) noexcept {
    std::size_t slots = 0;
    for (const std::byte value : mask) {
        slots += static_cast<std::size_t>(std::popcount(std::to_integer<unsigned char>(value)));
    }
    return slots;
}

[[nodiscard]] std::size_t
first_mask_slot(const service::entity_slots::EntitySlotMask& mask) noexcept {
    for (std::size_t byteIndex = 0; byteIndex < mask.size(); ++byteIndex) {
        const unsigned value = std::to_integer<unsigned char>(mask[byteIndex]);
        if (value != 0) {
            return byteIndex * service::entity_slots::kBitsPerMaskByte
                   + static_cast<std::size_t>(std::countr_zero(value));
        }
    }
    return service::entity_slots::kSlotCount;
}

[[nodiscard]] std::size_t
last_mask_slot(const service::entity_slots::EntitySlotMask& mask) noexcept {
    for (std::size_t byteIndex = mask.size(); byteIndex > 0; --byteIndex) {
        const unsigned value = std::to_integer<unsigned char>(mask[byteIndex - 1]);
        if (value != 0) {
            return (byteIndex - 1) * service::entity_slots::kBitsPerMaskByte
                   + static_cast<std::size_t>(std::bit_width(value) - 1U);
        }
    }
    return service::entity_slots::kSlotCount;
}

[[nodiscard]] bool report_authority_release(const service::Request& request,
                                            bool expectReason) noexcept {
    authority::Release decoded;
    const bool parsed = expectReason ? authority::parse_abandon(request.payload, decoded)
                                     : authority::parse_abdicate(request.payload, decoded);
    if (!parsed) {
        return false;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=authority result=noted type=%u "
                                      "selector=%u reason=%d slots=%zu first=%zu last=%zu",
                                      request.messageType,
                                      static_cast<unsigned>(decoded.selector),
                                      decoded.hasReason ? decoded.reason : 0,
                                      mask_slot_count(decoded.mask),
                                      first_mask_slot(decoded.mask),
                                      last_mask_slot(decoded.mask));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

[[nodiscard]] bool report_query_answer(const service::Request& request) noexcept {
    namespace authority = service::entity_authority;
    authority::QueryAnswer answer;
    if (!authority::parse_query_answer(request.messageType, request.payload, answer)) {
        return false;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=authority result=ok type=%u corr=0x%08X "
                                      "selector=%d mask=%u",
                                      request.messageType,
                                      answer.correlation,
                                      answer.hasSelector ? static_cast<int>(answer.selector) : -1,
                                      static_cast<unsigned>(answer.hasMask));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

// no answer: the reply is msg 25, whose consumer asserts unless the epoch is one above the client's, and nothing tracks that
[[nodiscard]] bool report_request_purge(const service::Request& request) noexcept {
    std::int32_t reason = 0;
    if (!service::entity_authority::parse_request_purge(request.payload, reason)) {
        return false;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(), line.size(), "ev=activity stage=purge result=noted reason=%d", reason);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

[[nodiscard]] bool prepare_release(const service::Request& request, ActivityPlan& plan) noexcept {
    service::entity_slots::EntitySlotMask decoded{};
    if (!service::entity_slots::decode_entity_slots(request.payload, decoded)) {
        return false;
    }
    state::activity::entity_slots::LeaseMask returned{};
    std::copy(decoded.begin(), decoded.end(), returned.begin());
    if (!state::activity::entity_slots::prepare_release(
            request.accountHandle, returned, plan.entitySlotMutation)) {
        return false;
    }
    plan.sessionId = request.accountHandle;
    plan.delivery = Delivery::none;
    plan.mutationDomain = MutationDomain::entitySlots;
    return true;
}

} // namespace

bool process(std::uint64_t boundSessionId,
             std::span<const std::byte> requestBody,
             ActivityPlan& plan,
             bool& hasTransaction,
             std::uint64_t boundJoinRevision,
             const middleware::bap::activity_message::patch_epoch::PatchEpoch* expectedEpoch,
             const middleware::bap::activity_message::RequestProvenance& provenance) noexcept {
    plan = {};
    hasTransaction = false;

    service::Request request;
    if (!service::parse_request(requestBody, request)) {
        report_message(0, 0, "parse");
        return false;
    }
    request.provenance=provenance;
    (void)expectedEpoch;
    // dispatch is on message type alone; the join arrives on a link that has allocated nothing
    bool prepared = false;
    if (request.messageType == epoch_message::kMessageType) {
        // type 52 alone carries a zero handle, so its session is the one this link allocated
        prepared = patch_epoch::prepare(boundSessionId, request, plan);
    } else if (request.messageType == high_water::kMessageType
               || request.messageType == client_keepalive::kMessageType) {
        return true;
    } else if (request.messageType == kJoinRequestMessageType) {
        prepared = prepare_join(request, plan);
    } else if (request.messageType == 13 || request.messageType == 14) {
        prepared = membership::prepare_reservations(request, plan);
    } else if (request.messageType == service::entity_slot_request::kMessageType) {
        prepared = prepare_grant(request, plan);
    } else if (request.messageType == service::entity_slots::kRequestMessageType) {
        prepared = prepare_release(request, plan);
    } else if (request.messageType == service::state_refresh::kMessageType) {
        prepared = membership::prepare_refresh(request, plan);
    } else if (request.messageType == service::client_identity::kMessageType) {
        prepared = membership::prepare_identity(request, plan);
    } else if (request.messageType == service::client_authoritative_data::kMessageType) {
        prepared = membership::prepare_authoritative(request, plan);
    } else if (request.messageType == service::membership_acknowledgement::kMessageType) {
        prepared = membership::prepare_acknowledgement(request, plan);
    } else if (request.messageType == authority::kAbandonMessageType) {
        namespace retirement=state::activity::entity_retirement;
        retirement::wire::Abandon release{};
        if (!retirement::wire::parse_abandon(request.payload,release)) {
            report_message(request.messageType, request.accountHandle, "parse");
        } else if (boundSessionId!=0 && boundSessionId==request.accountHandle) {
            const auto result=retirement::abandon(request.accountHandle,
                state::account_snapshot().primarySoid,release);
            using Result=retirement::History::Result;
            core::log::write(core::log::Channel::server,
                result==Result::queued?core::log::Level::info:core::log::Level::debug,
                result==Result::queued?"ev=activity stage=retirement result=queued source=native26_view"
                :result==Result::duplicate?"ev=activity stage=retirement result=duplicate"
                :result==Result::capacity?"ev=activity stage=retirement result=refused reason=history_capacity"
                :"ev=activity stage=retirement result=refused reason=departure_provenance");
        } else {
            report_message(request.messageType,request.accountHandle,"retirement_binding");
        }
        return true;
    } else if (request.messageType == authority::kAbdicateMessageType) {
        if (!report_authority_release(request, false)) {
            report_message(request.messageType, request.accountHandle, "parse");
        }
        return true;
    } else if (request.messageType == service::incident::kMessageType) {
        report_incident(request);
        report_crucible_incident(boundSessionId, request);
        if(boundSessionId!=0 && boundSessionId==request.accountHandle)
            state::activity::crucible::runtime::observe_incident(boundSessionId,request.payload);
        mission_sense::observe_incident(boundSessionId, request);
        return true;
    } else if (request.messageType == authority::kRequestPurgeMessageType) {
        if (!report_request_purge(request)) {
            report_message(request.messageType, request.accountHandle, "parse");
        }
        return true;
    } else if (request.messageType == authority::kQueryPerBubbleMessageType
               || request.messageType == authority::kQueryResponseMessageType) {
        namespace retirement=state::activity::entity_retirement;
        if (boundSessionId!=0 && boundSessionId==request.accountHandle && boundJoinRevision!=0) {
            const auto result=retirement::answer_query(boundSessionId,
                state::account_snapshot().primarySoid,boundJoinRevision,request.messageType,
                request.payload,GetTickCount64());
            if (result==retirement::Queries::Reply::queued || result==retirement::Queries::Reply::completed)
                core::log::write(core::log::Channel::server,core::log::Level::info,
                    result==retirement::Queries::Reply::queued
                    ?"ev=activity stage=departure_cleanup result=queued source=survivor_native31"
                    :"ev=activity stage=departure_cleanup result=complete source=survivor_native31");
        }
        return true;
    } else if (request.messageType == authority::kResetAcknowledgementMessageType) {
        if (!report_query_answer(request)) {
            report_message(request.messageType, request.accountHandle, "parse");
        }
        return true;
    } else if (const char* name = accepted_name(request.messageType); name != nullptr) {
        if (request.messageType == kSenseUpdateMessageType) {
            mission_sense::observe_sense_update(boundSessionId, request);
        }
        report_accepted(request.messageType, name, request.payload);
        return true;
    } else {
        report_message(request.messageType, request.accountHandle, "unhandled");
        return true;
    }
    // report and drop an unstageable message; failing the frame would jam the client's pending ring
    if (!prepared) {
        report_message(request.messageType, request.accountHandle, "prepare");
        plan = {};
        return true;
    }
    plan.requestMessageType = request.messageType;
    hasTransaction = true;
    return true;
}

} // namespace dawn::server::bap::encrypted::activity_message
