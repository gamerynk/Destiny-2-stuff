#include <WinSock2.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "../../core/logging/log.h"
#include "../bap/runtime.h"
#include "internal.h"

namespace dawn::server::transport {
namespace {

[[nodiscard]] std::size_t frame_size(std::span<const std::byte> prefix) noexcept {
    if (prefix.size() < kOuterHeaderSize) {
        return 0;
    }
    std::uint32_t payload = 0;
    for (std::size_t index = 0; index < sizeof payload; ++index) {
        payload =
            (payload << 8U) | std::to_integer<std::uint32_t>(prefix[kOuterLengthOffset + index]);
    }
    const std::size_t total = kOuterHeaderSize + payload;
    return total <= kStreamCapacity ? total : 0;
}

} // namespace

bool offer(std::size_t slot,
           client::network::BapEvent event,
           std::span<const std::byte> frame) noexcept {
    Peer& peer = g_listener.peers[slot];
    if (peer.outputSize != 0) {
        return false;
    }
    client::network::BapResponse response{};
    const client::network::BapRequest request{event, connection_id(slot), frame, peer.output};
    if (!bap::consume(request, response) || response.size == 0) {
        return true;
    }
    if (response.size > peer.output.size()) {
        return false;
    }
    peer.outputOffset = 0;
    peer.outputSize = response.size;
    return true;
}

bool drain_stream(std::size_t slot) noexcept {
    Peer& peer = g_listener.peers[slot];
    if (peer.outputSize != 0) {
        return true;
    }
    const auto pending = std::span(peer.stream).first(peer.streamSize);
    const std::size_t total = frame_size(pending);
    if (total == 0) {
        return peer.streamSize != kStreamCapacity;
    }
    if (pending.size() < total) {
        return true;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int count = std::snprintf(line.data(),
                                    line.size(),
                                    "ev=transport stage=frame conn=%u type=%u bytes=%zu",
                                    connection_id(slot),
                                    static_cast<unsigned>(pending[1]),
                                    total);
    if (count > 0) {
        const std::size_t length = static_cast<std::size_t>(count) < line.size()
                                       ? static_cast<std::size_t>(count)
                                       : line.size() - 1;
        core::log::write(
            core::log::Channel::server, core::log::Level::debug, {line.data(), length});
    }
    if (!offer(slot, client::network::BapEvent::frame, pending.first(total))) {
        return false;
    }
    peer.streamSize -= total;
    if (peer.streamSize != 0) {
        std::memmove(peer.stream.data(), peer.stream.data() + total, peer.streamSize);
    }
    return true;
}

bool advance_output(Peer& peer, std::size_t sent) noexcept {
    if (sent == 0 || peer.outputSize == 0 || peer.outputOffset >= peer.outputSize
        || sent > peer.outputSize - peer.outputOffset) {
        return false;
    }
    peer.outputOffset += sent;
    if (peer.outputOffset == peer.outputSize) {
        peer.outputOffset = 0;
        peer.outputSize = 0;
    }
    return true;
}

void close_peer(std::size_t slot, const char* reason, int socketError) noexcept {
    Peer& peer = g_listener.peers[slot];
    if (peer.socket == INVALID_SOCKET) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=transport stage=close conn=%u reason=%s wsa=%d stream_bytes=%zu output_offset=%zu output_bytes=%zu",
        connection_id(slot),
        reason != nullptr ? reason : "unknown",
        socketError,
        peer.streamSize,
        peer.outputOffset,
        peer.outputSize);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
    client::network::BapResponse response{};
    const client::network::BapRequest request{
        client::network::BapEvent::close, connection_id(slot), {}, {}};
    (void)bap::consume(request, response);
    closesocket(peer.socket);
    peer.socket = INVALID_SOCKET;
    peer.streamSize = 0;
    peer.outputOffset = 0;
    peer.outputSize = 0;
}

} // namespace dawn::server::transport
