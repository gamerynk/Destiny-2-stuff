#include "activity_message_push.h"

#include <Windows.h>

#include <algorithm>

#include "../../../../../middleware/bap/activity_message/activity_join_result_encoder.h"
#include "../../../../../middleware/bap/activity_message/entity_slots.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "activity_global_state_push.h"
#include "activity_notification_frame.h"
#include "activity_retirement_push.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

namespace service = middleware::bap::activity_message;

constexpr std::uint32_t kJoinResultMessageType = 4;
constexpr std::uint32_t kBubbleHostStateMessageType = 54;
// message 54 opens with a 6-bit host count; zero carries no records, so the body is one zero byte
constexpr std::array<std::byte, 1> kEmptyBubbleHostState{};
// 5 s stops the zero-hint keepalive flood seen locally
constexpr std::uint16_t kLocalKeepaliveHintMilliseconds = 5'000;

void clear_prefix(std::span<std::byte> buffer, std::size_t size) noexcept {
    SecureZeroMemory(buffer.data(), (std::min)(buffer.size(), size));
}

} // namespace

bool append_join_notifications(Scratch& scratch,
                               const activity_message::ActivityPlan& activity,
                               std::span<const std::byte, state::kAesKeySize> key,
                               std::array<std::byte, state::kBapNonceSize>& nonce,
                               std::span<std::byte> response,
                               std::size_t& written) noexcept {
    if (written > response.size()) {
        return false;
    }
    const std::size_t initialWritten = written;
    auto initialNonce = nonce;
    std::size_t messageSize = 0;
    bool encoded = service::join_result::encode_join_result(activity.correlation,
                                                            activity.sessionId,
                                                            kLocalKeepaliveHintMilliseconds,
                                                            scratch.responseBody,
                                                            messageSize,
                                                            static_cast<std::uint8_t>(
                                                                activity.entitySlotMutation.retirement.managed
                                                                ?activity.entitySlotMutation.retirement.join.before&0xFF:0))
                   && append_notification_frame(scratch,
                                                activity.sessionId,
                                                kJoinResultMessageType,
                                                std::span(scratch.responseBody).first(messageSize),
                                                key,
                                                nonce,
                                                response,
                                                written);
    clear_prefix(scratch.responseBody, messageSize);
    if (encoded) {
        middleware::secure_channel::advance_nonce(nonce);
        const auto& history=activity.entitySlotMutation.retirement;
        // a retained native view may still hold an old character; replay owed generations after accepted4 but before lease0/global1/roster
        if (history.managed) for (auto n=history.join.before;encoded && n<history.join.through;) {
            state::activity::entity_retirement::History::Operation operation{};
            encoded=state::activity::entity_retirement::join_operation(history,++n,operation)
                && append_retirement(scratch,activity.sessionId,operation,key,nonce,response,written);
        }
    }
    if (encoded) {
        encoded = append_entity_slot_notification(scratch,
                                                  activity.sessionId,
                                                  activity.entitySlotMutation.mask,
                                                  key,
                                                  nonce,
                                                  response,
                                                  written);
    }
    // order: 4 first (the only message the pre-join arm accepts), then 1 (step 33 reads the activity name from it), 54 closes with its empty host table
    if (encoded) {
        encoded = append_global_state_notification(
            scratch, activity.sessionId, key, nonce, response, written,
            activity.entitySlotMutation.importsShared ? &activity.entitySlotMutation.sharedRecord.destination : nullptr);
    }
    if (encoded) {
        encoded = append_notification_frame(scratch,
                                            activity.sessionId,
                                            kBubbleHostStateMessageType,
                                            kEmptyBubbleHostState,
                                            key,
                                            nonce,
                                            response,
                                            written);
        if (encoded) {
            middleware::secure_channel::advance_nonce(nonce);
        }
    }
    if (!encoded) {
        clear_prefix(response.subspan(initialWritten), written - initialWritten);
        written = initialWritten;
        nonce = initialNonce;
    }
    SecureZeroMemory(&initialNonce, sizeof initialNonce);
    return encoded;
}

bool append_entity_slot_notification(Scratch& scratch,
                                     std::uint64_t sessionId,
                                     std::span<const std::byte> entitySlots,
                                     std::span<const std::byte, state::kAesKeySize> key,
                                     std::array<std::byte, state::kBapNonceSize>& nonce,
                                     std::span<std::byte> response,
                                     std::size_t& written) noexcept {
    if (written > response.size() || entitySlots.size() != service::entity_slots::kEncodedSize) {
        return false;
    }

    const std::size_t initialWritten = written;
    auto initialNonce = nonce;
    std::size_t messageSize = 0;
    const std::span<const std::byte, service::entity_slots::kEncodedSize> selected{
        entitySlots.data(), entitySlots.size()};
    const bool encoded =
        service::entity_slots::encode_entity_slots(selected, scratch.responseBody, messageSize)
        && append_notification_frame(scratch,
                                     sessionId,
                                     service::entity_slots::kNotificationMessageType,
                                     std::span(scratch.responseBody).first(messageSize),
                                     key,
                                     nonce,
                                     response,
                                     written);
    clear_prefix(scratch.responseBody, messageSize);
    if (encoded) {
        middleware::secure_channel::advance_nonce(nonce);
    } else {
        clear_prefix(response.subspan(initialWritten), written - initialWritten);
        written = initialWritten;
        nonce = initialNonce;
    }
    SecureZeroMemory(&initialNonce, sizeof initialNonce);
    return encoded;
}

} // namespace dawn::server::bap::encrypted::push::activity
