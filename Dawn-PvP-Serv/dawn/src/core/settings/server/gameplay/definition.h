#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::core::settings::server::gameplay {

enum class Topology : std::uint8_t {
    disabled,
    embedded,
    external,
};

inline constexpr std::size_t kAddressOctets = 4;
// the join descriptor carries an even UDP port, so an odd port is refused at load
inline constexpr std::uint16_t kPortAlignment = 2;
inline constexpr std::uint16_t kDefaultPort = 30976;
// discovery owns 3074 and 3075, so the gameplay endpoint can't take either
inline constexpr std::uint16_t kDiscoveryPortLow = 3074;
inline constexpr std::uint16_t kDiscoveryPortHigh = 3075;
inline constexpr std::uint16_t kDefaultServerReserve = 256;
inline constexpr std::uint16_t kMinimumServerReserve = 8;
inline constexpr std::uint16_t kClientLeaseMinimum = 4096;

struct Settings {
    Topology topology{Topology::embedded};
    std::array<unsigned char, kAddressOctets> bindAddress{127, 0, 0, 1};
    std::array<unsigned char, kAddressOctets> advertisedAddress{127, 0, 0, 1};
    std::array<unsigned char, kAddressOctets> transportAddress{127, 0, 0, 1};
    // one port for all three addresses because the egress hook keeps the port
    std::uint16_t port{kDefaultPort};
    std::uint16_t serverReserveCount{kDefaultServerReserve};
};

[[nodiscard]] bool valid(const Settings& settings) noexcept;

[[nodiscard]] std::uint16_t effective_reserve(const Settings& settings) noexcept;

} // namespace dawn::core::settings::server::gameplay
