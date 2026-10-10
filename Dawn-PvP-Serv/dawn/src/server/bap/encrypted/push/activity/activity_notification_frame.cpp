#include "activity_notification_frame.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../middleware/bap/activity_message/activity_message_notification_encoder.h"
#include "../../../../../middleware/bap/frame.h"
#include "../../../../../middleware/secure_channel/runtime.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

namespace service = middleware::bap::activity_message;

constexpr std::uint32_t kNotificationSequence = 0;

constexpr std::size_t kLineCapacity = 128;

void report(std::uint64_t sessionId,
            std::uint32_t messageType,
            std::size_t bodySize,
            std::size_t frameSize) noexcept {
    std::array<char, kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activity stage=push result=%s type=%u soid=0x%llX "
                                      "body=%zu len=%zu",
                                      frameSize != 0 ? "ok" : "fail",
                                      messageType,
                                      static_cast<unsigned long long>(sessionId),
                                      bodySize,
                                      frameSize);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         frameSize != 0 ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

void clear_prefix(std::span<std::byte> buffer, std::size_t size) noexcept {
    SecureZeroMemory(buffer.data(), (std::min)(buffer.size(), size));
}

} // namespace

bool append_notification_frame(Scratch& scratch,
                               std::uint64_t sessionId,
                               std::uint32_t messageType,
                               std::span<const std::byte> messageBody,
                               std::span<const std::byte, state::kAesKeySize> key,
                               std::span<const std::byte, state::kBapNonceSize> nonce,
                               std::span<std::byte> response,
                               std::size_t& written) noexcept {
    if (written > response.size()) {
        return false;
    }

    std::size_t activitySize = 0;
    std::size_t payloadSize = 0;
    std::size_t sealedSize = 0;
    std::size_t frameSize = 0;
    const bool encoded =
        service::encode_notification(
            sessionId, messageType, messageBody, scratch.responsePayload, activitySize)
        && middleware::bap::encode_notification_payload(
            middleware::bap::NotificationService::activityMessage,
            kNotificationSequence,
            std::span(scratch.responsePayload).first(activitySize),
            scratch.sealed,
            payloadSize)
        && middleware::secure_channel::seal_frame(
            key, nonce, std::span(scratch.sealed).first(payloadSize), scratch.plaintext, sealedSize)
        && middleware::bap::encode_frame(middleware::bap::FrameType::encrypted,
                                         std::span(scratch.plaintext).first(sealedSize),
                                         response.subspan(written),
                                         frameSize);
    if (encoded) {
        written += frameSize;
    }
    report(sessionId, messageType, messageBody.size(), encoded ? frameSize : 0);
    clear_prefix(scratch.responsePayload, activitySize);
    clear_prefix(scratch.sealed, payloadSize);
    // a failed seal may have touched its tag-sized output before reporting no bytes written
    clear_prefix(scratch.plaintext, payloadSize + middleware::secure_channel::kFrameTagSize);
    return encoded;
}

} // namespace dawn::server::bap::encrypted::push::activity
