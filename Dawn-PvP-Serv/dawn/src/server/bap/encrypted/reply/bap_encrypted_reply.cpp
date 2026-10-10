#include <Windows.h>

#include <algorithm>

#include "../../../../middleware/secure_channel/runtime.h"
#include "../internal.h"

namespace dawn::server::bap::encrypted::reply {

bool encode(Scratch& scratch,
            const ServiceRoute& route,
            std::uint32_t taskId,
            std::span<const std::byte, state::kAesKeySize> key,
            std::span<const std::byte, state::kBapNonceSize> nonce,
            std::span<const std::byte> body,
            std::span<std::byte> response,
            std::size_t& written,
            std::uint16_t status) noexcept {
    if (written > response.size()) {
        return false;
    }
    std::size_t payloadSize = 0;
    std::size_t sealedSize = 0;
    std::size_t framedSize = 0;
    const bool encoded =
        middleware::bap::encode_response_payload(
            route.response, taskId, body, scratch.responsePayload, payloadSize, status)
        && middleware::secure_channel::seal_frame(
            key,
            nonce,
            std::span(scratch.responsePayload).first(payloadSize),
            scratch.sealed,
            sealedSize)
        && middleware::bap::encode_frame(middleware::bap::FrameType::encrypted,
                                         std::span(scratch.sealed).first(sealedSize),
                                         response.subspan(written),
                                         framedSize);
    SecureZeroMemory(scratch.responsePayload.data(),
                     (std::min)(scratch.responsePayload.size(), payloadSize));
    SecureZeroMemory(scratch.sealed.data(), (std::min)(scratch.sealed.size(), sealedSize));
    if (encoded) {
        written += framedSize;
    }
    return encoded;
}

} // namespace dawn::server::bap::encrypted::reply
