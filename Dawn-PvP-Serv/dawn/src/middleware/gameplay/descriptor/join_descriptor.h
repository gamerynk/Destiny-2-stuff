#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::gameplay::descriptor {

// the descriptor is exactly 128 bytes; any other count reads as absent
inline constexpr std::size_t kDescriptorSize = 128;

inline constexpr std::size_t kNetAddrSize = 86;

struct JoinEndpoint {
    // zero makes the descriptor read as absent
    std::uint64_t machineId{};
    std::uint32_t address{};
    // must be even
    std::uint16_t port{};
    // its low 64 bits are the lobby the client joins
    std::uint64_t onlineSessionId{};
};

// the endpoint goes in twice, as local entry 0 and as the public entry; a peer with no
// public entry is unroutable and the client falls back to NAT traversal
void write_net_addr(std::uint32_t address,
                    std::uint16_t port,
                    std::array<std::byte, kNetAddrSize>& output) noexcept;

[[nodiscard]] bool build(const JoinEndpoint& endpoint,
                         std::array<std::byte, kDescriptorSize>& output) noexcept;

} // namespace dawn::middleware::gameplay::descriptor
