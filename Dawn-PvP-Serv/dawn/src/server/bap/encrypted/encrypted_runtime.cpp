#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../../core/logging/log.h"
#include "../../../middleware/secure_channel/runtime.h"
#include "../../../middleware/web_service/web_service_envelope.h"
#include "../../../state/runtime/runtime.h"
#include "../internal.h"
#include "activity_transaction/activity_transaction_notifications.h"
#include "bap_connection_publication.h"
#include "internal.h"
#include "push/activity/activity_roster_push.h"
#include "queuez/queuez_outcome_staging.h"
#include "transactions/service_outcome_commit.h"

#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../RestorationServer/src/realtime_store.h"
#endif

namespace dawn::server::bap::encrypted {
namespace {

void clear_prefix(std::span<std::byte> buffer, std::size_t size) noexcept {
    SecureZeroMemory(buffer.data(), (std::min)(buffer.size(), size));
}


} // namespace

bool consume(Session& session,
             Scratch& scratch,
             const middleware::bap::OuterFrame& outer,
             std::span<std::byte> response,
             std::size_t& written) noexcept {
    written = 0;
    session.accountMutationPublished = false;
    if (!session.authenticated) {
        // silence looks like a decode fault, and both look like a dead link
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap stage=encrypted result=drop reason=unauthenticated");
        return false;
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    if (now <= 0
        || !state::session::active(session.credentials, static_cast<std::uint64_t>(now))) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap stage=encrypted result=drop reason=session_expired");
        return false;
    }

    std::size_t plaintextSize = 0;
    if(session.authenticatedOrdinal==(std::numeric_limits<std::uint64_t>::max)())return false;
    const auto& sessionKey = session.credentials.bap.sessionKey;
    if (!middleware::secure_channel::open_frame(sessionKey,
                                                session.receiveNonce,
                                                outer.payload,
                                                scratch.plaintext,
                                                plaintextSize)) {
        const std::size_t possiblePlaintextSize =
            outer.payload.size() >= middleware::secure_channel::kFrameTagSize
                ? outer.payload.size() - middleware::secure_channel::kFrameTagSize
                : 0;
        clear_prefix(scratch.plaintext, possiblePlaintextSize);
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=none stage=decrypt result=fail");
        return false;
    }
    // authentication consumes the receive nonce even when the inner service is unsupported
    middleware::secure_channel::advance_nonce(session.receiveNonce);
    ++session.authenticatedOrdinal;

    middleware::bap::RequestFrame frame;
    ServiceRoute route;
    std::size_t responseBodySize = 0;
    std::size_t framedSize = 0;
    thread_local ServiceOutcome outcome;
    reset(outcome);
    transactions::Publication publication{};
    queuez::SessionState nextQueuez = session.queuez;
    bool publishesQueuez = false;
    bool handled =
        middleware::bap::parse_request_payload(std::span(scratch.plaintext).first(plaintextSize),
                                               middleware::bap::FrameType::encrypted,
                                               frame)
        && routing::resolve(frame.messageId, route);
    if (!handled) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         "ev=bap svc=none stage=parse result=fail");
    }
    const bool processesBody = handled && route.responseMode != ResponseMode::none;
    const bool sendsReply = handled && route.responseMode == ResponseMode::reply;
    if (processesBody
        && !body::process(route,
                          session.queuez,
                          session.activitySessionId,
                          session.matchmakingContext,
                          session.credentials.accountSoid,
                          session.credentials.platformId,
                          frame.body,
                          scratch.responseBody,
                          responseBodySize,
                          outcome,session.activityRetirementJoinRevision,
                          session.activityPatchEpochSeen?&session.activityPatchEpoch:nullptr,
                          {session.connectionIncarnation,session.authenticatedOrdinal,frame.taskId})) {
        diagnostics::report_failure(frame.messageId, "body");
        // a reply-mode service answers with an empty body; one unanswered request jams the client's pending ring for good
        clear_prefix(scratch.responseBody, responseBodySize);
        responseBodySize = 0;
        reset(outcome);
        handled = sendsReply;
    }
    // stage every frame and check caller room before committing State or the nonce
    auto nextSendNonce = session.sendNonce;
    queuez::StagedPublication queuezPublication{};
    thread_local std::array<std::byte, client::network::kBapFrameCapacity> inlineUpdateBytes{};
    thread_local std::array<std::byte, client::network::kBapFrameCapacity> inlineReplyBytes{};
    InlineSubscriptionUpdate inlineUpdate{inlineUpdateBytes, 0};
    middleware::web_service::Message inlineMessage{};
    const bool inlineSubscription = sendsReply && route.bodyCodec == BodyCodec::webService
        && (route.response == middleware::bap::ResponseService::webService
            || route.response == middleware::bap::ResponseService::webServiceServer)
        && middleware::web_service::parse_request(frame.body, inlineMessage) && inlineMessage.opcode == 206;
    bool responseFailure = outcome.responseStatus != 200;
    const auto responseStatus = outcome.responseStatus;
    if (responseFailure) {
        push::activity::discard_staged_roster(session);
        reset(outcome);
        responseBodySize = 0;
    }
    std::size_t originalReplySize = 0;
    if (handled && sendsReply) {
        handled = reply::encode(scratch,
                                route,
                                frame.taskId,
                                sessionKey,
                                nextSendNonce,
                                std::span(scratch.responseBody).first(responseBodySize),
                                scratch.framed,
                                framedSize, responseStatus);
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "encode");
        } else {
            middleware::secure_channel::advance_nonce(nextSendNonce);
            originalReplySize = framedSize;
        }
    }
    if (handled && !responseFailure) {
        handled = queuez::stage_service_outcome(scratch,
                                                session.queuez,
                                                outcome,
                                                sessionKey,
                                                nextSendNonce,
                                                scratch.framed,
                                                framedSize,
                                                queuezPublication,
                                                inlineSubscription && outcome.hasSubscription ? &inlineUpdate : nullptr);
        if (handled && queuezPublication.hasState) {
            nextQueuez = queuezPublication.after;
            publishesQueuez = true;
        }
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "stage");
        }
    }
    if (handled && inlineSubscription && inlineUpdate.size != 0) {
        std::size_t replacementSize = 0;
        handled = middleware::web_service::encode_response(inlineMessage,
                middleware::web_service::ResponseShape::statusOnly, {}, scratch.responseBody,
                responseBodySize, std::span(inlineUpdate.bytes).first(inlineUpdate.size))
            && reply::encode(scratch, route, frame.taskId, sessionKey, session.sendNonce,
                std::span(scratch.responseBody).first(responseBodySize), inlineReplyBytes, replacementSize)
            && originalReplySize <= framedSize
            && replacementSize <= scratch.framed.size() - (framedSize - originalReplySize);
        if (handled) {
            const auto trailing = framedSize - originalReplySize;
            std::memmove(scratch.framed.data() + replacementSize,
                         scratch.framed.data() + originalReplySize, trailing);
            std::copy_n(inlineReplyBytes.begin(), replacementSize, scratch.framed.begin());
            framedSize = replacementSize + trailing;
            core::log::write(core::log::Channel::server, core::log::Level::info,
                "ev=queuez stage=subscription_reply result=inline source=existing_snapshot");
        } else diagnostics::report_failure(frame.messageId, "inline_subscription");
        clear_prefix(inlineUpdateBytes, inlineUpdate.size);
        clear_prefix(inlineReplyBytes, replacementSize);
    }
    if (inlineSubscription && (!handled || inlineUpdate.size == 0)) {
        // native WS error handling fails the transport; discard every staged snapshot and nonce, send one outer error
        const std::uint16_t status = outcome.hasSubscription ? 503 : 400;
        responseFailure = true;
        push::activity::discard_staged_roster(session);
        reset(outcome);
        SecureZeroMemory(&queuezPublication, sizeof queuezPublication);
        publishesQueuez = false;
        nextQueuez = session.queuez;
        nextSendNonce = session.sendNonce;
        clear_prefix(scratch.framed, framedSize);
        framedSize = 0;
        handled = reply::encode(scratch, route, frame.taskId, sessionKey, nextSendNonce,
                                {}, scratch.framed, framedSize, status);
        if (handled) middleware::secure_channel::advance_nonce(nextSendNonce);
        core::log::write(core::log::Channel::server, core::log::Level::warn,
            status == 400
                ? "ev=queuez stage=subscription_reply result=refused status=400 native_transport_failure=1"
                : "ev=queuez stage=subscription_reply result=unavailable status=503 native_transport_failure=1");
        clear_prefix(inlineUpdateBytes, inlineUpdate.size);
    }
    const auto* activityPlan = transaction_if<activity_message::ActivityPlan>(outcome);
    if (handled && activityPlan != nullptr) {
        handled = route.responseMode == ResponseMode::uncorrelatedPush;
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "route");
        } else if (!activity_transaction::stage_notifications(session,
                                                              scratch,
                                                              *activityPlan,
                                                              sessionKey,
                                                              nextSendNonce,
                                                              scratch.framed,
                                                              framedSize)) {
            // still commit; an unbuildable push is one lost message, and dropping the commit would strand the client's reported state
            diagnostics::report_failure(frame.messageId, "notify");
            if (activityPlan->delivery==activity_message::Delivery::joinNotifications) {
                handled=false;
            }
        }
    }
    const bool mutatesAccount =
        outcome.hasChangeCharacter || outcome.hasSelectCharacter
        || transaction_if<EquipmentSwapTransaction>(outcome) != nullptr
        || transaction_if<SocketPlugTransaction>(outcome) != nullptr
        || transaction_if<ItemStateTransaction>(outcome) != nullptr
        || transaction_if<ItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<ProfileItemAcquisitionTransaction>(outcome) != nullptr
        || transaction_if<ItemDismantleTransaction>(outcome) != nullptr
        || transaction_if<AbilitySelectionTransaction>(outcome) != nullptr;
#if defined(RESTORATION_DEDICATED_SERVER)
    // read before commit; committing clears the staged transaction flags
    const bool commitsCharacterSelection = outcome.hasSelectCharacter;
#endif
    const auto* stagedSocket = transaction_if<SocketPlugTransaction>(outcome);
    const std::uint8_t socketLane = stagedSocket == nullptr ? 0 : stagedSocket->pending.socketLane;
    const std::uint16_t socketPlugDefinition =
        stagedSocket == nullptr ? 0 : stagedSocket->pending.plugDefinitionIndex;
    const std::uint8_t socketTargetBucket =
        stagedSocket == nullptr ? 0 : stagedSocket->pending.targetBucketId;
    const std::uint8_t socketPlugBucket =
        stagedSocket == nullptr ? 0 : stagedSocket->pending.plugBucketId;
    const auto* stagedItemState = transaction_if<ItemStateTransaction>(outcome);
    const std::uint64_t itemStateInstance =
        stagedItemState == nullptr ? 0 : stagedItemState->pending.targetInstanceSoid;
    const std::uint32_t itemStateFlags =
        stagedItemState == nullptr ? 0 : stagedItemState->pending.afterFlags;
    const auto* stagedProfile = transaction_if<ProfileItemAcquisitionTransaction>(outcome);
    const std::uint32_t profileDefinitionHash =
        stagedProfile == nullptr ? 0 : stagedProfile->pending.acquiredDefinitionHash;
    const std::int32_t profileQuantity =
        stagedProfile == nullptr ? 0 : stagedProfile->pending.acquiredQuantity;
    const bool profileActionSource =
        stagedProfile != nullptr && stagedProfile->pending.actionSource;
    const bool profileAppended = stagedProfile != nullptr && stagedProfile->pending.appended;
    // committing clears the mutation the member key lives in, so capture connection fields first
    const ConnectionFields connection = connection_fields(outcome);
    if (handled && processesBody) {
        handled = framedSize <= response.size()
            && (responseFailure || transactions::commit(outcome, publication));
        if (!handled) {
            diagnostics::report_failure(frame.messageId, "commit");
        }
        if (handled) {
            std::copy_n(scratch.framed.begin(), framedSize, response.begin());
            written = framedSize;
            session.sendNonce = nextSendNonce;
            if (publishesQueuez) {
                session.queuez = nextQueuez;
            }
            if (!responseFailure) {
                arm_repushes(session, queuezPublication);
                publish_connection_fields(session, publication, connection);
            }
#if defined(RESTORATION_DEDICATED_SERVER)
            if (commitsCharacterSelection) {
                const bool ready = ::restoration::realtime_store::mark_gameplay_ready(
                    session.credentials.platformId,
                    session.credentials.accountSoid,
                    session.credentials.generation,
                    static_cast<std::uint64_t>(now));
                core::log::write(
                    core::log::Channel::server,
                    ready ? core::log::Level::info : core::log::Level::warn,
                    ready
                        ? "ev=presence stage=character_commit result=ready"
                        : "ev=presence stage=character_commit result=fail reason=session_generation");
            }
#endif
            if (!responseFailure) push::activity::commit_staged_roster(session);
            session.accountMutationPublished = mutatesAccount;
            if (transaction_if<EquipmentSwapTransaction>(outcome) != nullptr) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=equip stage=output_publish result=ok framed_bytes=%zu queuez_published=%u "
                    "family_version=%d family0_version=%d family3_version=%d",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    session.queuez.family0Version,
                    session.queuez.family3Version);
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<SocketPlugTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=socket_plug stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d family0_version=%d "
                    "family3_version=%d instance=0x%llX lane=%u "
                    "plug_definition=%u target_bucket=%u plug_bucket=%u",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    session.queuez.family0Version,
                    session.queuez.family3Version,
                    static_cast<unsigned long long>(transaction->update.targetInstanceSoid),
                    static_cast<unsigned>(socketLane),
                    static_cast<unsigned>(socketPlugDefinition),
                    static_cast<unsigned>(socketTargetBucket),
                    static_cast<unsigned>(socketPlugBucket));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<ItemStateTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=item_state stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d instance=0x%llX flags=0x%X",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned long long>(itemStateInstance),
                    itemStateFlags);
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<ItemAcquisitionTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=acquire stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d residents=%u instance=0x%llX",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned>(session.queuez.family4ResidentCount),
                    static_cast<unsigned long long>(transaction->update.acquiredInstanceSoid));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction =
                    transaction_if<ProfileItemAcquisitionTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=profile_acquire stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d residents=%u definition_hash=0x%08X "
                    "quantity=%d instance=0x%llX action_source=%u appended_row=%u "
                    "appended_resident=%u",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned>(session.queuez.family4ResidentCount),
                    profileDefinitionHash,
                    profileQuantity,
                    static_cast<unsigned long long>(transaction->update.acquiredInstanceSoid),
                    static_cast<unsigned>(profileActionSource),
                    static_cast<unsigned>(profileAppended),
                    static_cast<unsigned>(transaction->update.appendedResident));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
            if (const auto* transaction = transaction_if<ItemDismantleTransaction>(outcome)) {
                std::array<char, core::log::kLineCapacity> line{};
                const int count = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=dismantle stage=output_publish result=ok framed_bytes=%zu "
                    "queuez_published=%u family_version=%d residents=%u instance=0x%llX",
                    framedSize,
                    static_cast<unsigned>(publishesQueuez),
                    session.queuez.family4Version,
                    static_cast<unsigned>(session.queuez.family4ResidentCount),
                    static_cast<unsigned long long>(transaction->update.dismantledInstanceSoid));
                if (count > 0) {
                    core::log::write(core::log::Channel::server,
                                     core::log::Level::debug,
                                     {line.data(), static_cast<std::size_t>(count)});
                }
            }
        }
    }
    if (!handled) {
        push::activity::discard_staged_roster(session);
    }
    clear_prefix(scratch.plaintext, plaintextSize);
    clear_prefix(scratch.responseBody, responseBodySize);
    clear_prefix(scratch.framed, framedSize);
    reset(outcome);
    SecureZeroMemory(&publication, sizeof publication);
    SecureZeroMemory(&queuezPublication, sizeof queuezPublication);
    if (handled) {
        core::log::write(core::log::Channel::server, core::log::Level::info, route.successEvent);
    }
    return handled;
}

} // namespace dawn::server::bap::encrypted
