#pragma once

#include <WinSock2.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../client/network/consumer.h"

namespace dawn::server::transport {

inline constexpr std::size_t kOuterHeaderSize = 6;
inline constexpr std::size_t kOuterLengthOffset = 2;
inline constexpr int kServiceIntervalMs = 50;
inline constexpr std::size_t kStreamCapacity = client::network::kBapFrameCapacity;

struct Peer {
    SOCKET socket{INVALID_SOCKET};
    std::size_t streamSize{};
    std::array<std::byte, kStreamCapacity> stream{};
    std::size_t outputOffset{};
    std::size_t outputSize{};
    std::array<std::byte, client::network::kBapFrameCapacity> output{};
};

struct Listener {
    bool active{};
    bool winsockOwned{};
    SOCKET acceptor{INVALID_SOCKET};
    std::uint64_t nextPollTick{};
    std::array<Peer, client::network::kBapConnectionCount> peers{};
};

extern Listener g_listener;

[[nodiscard]] constexpr std::uint32_t connection_id(std::size_t slot) noexcept {
    return static_cast<std::uint32_t>(slot + 1);
}

[[nodiscard]] bool
offer(std::size_t slot, client::network::BapEvent event, std::span<const std::byte> frame) noexcept;

[[nodiscard]] bool drain_stream(std::size_t slot) noexcept;

[[nodiscard]] bool advance_output(Peer& peer, std::size_t sent) noexcept;

void close_peer(std::size_t slot, const char* reason, int socketError = 0) noexcept;

[[nodiscard]] bool initialize_on_port(std::uint16_t port) noexcept;

} // namespace dawn::server::transport
