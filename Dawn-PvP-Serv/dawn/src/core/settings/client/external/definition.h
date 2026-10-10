#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::core::settings::client::external {

inline constexpr std::size_t kHostCapacity = 16;
inline constexpr std::size_t kAddressOctets = 4;
inline constexpr std::size_t kConfigUrlCapacity = 128;
inline constexpr std::size_t kOriginCapacity = 64;
inline constexpr std::uint16_t kDefaultHttpPort = 30975;
inline constexpr std::size_t kPortRouteCapacity = 8;

struct PortRoute {
    std::uint16_t sourcePort{};
    std::array<unsigned char, kAddressOctets> address{};
    std::uint16_t targetPort{};
};

struct Settings {
    bool enabled{true};
    std::array<char, kHostCapacity> host{"127.0.0.1"};
    std::array<wchar_t, kHostCapacity> hostWide{L"127.0.0.1"};
    std::array<unsigned char, kAddressOctets> address{127, 0, 0, 1};
    std::array<char, kOriginCapacity> origin{"http://127.0.0.1:30975"};
    std::uint16_t httpPort{kDefaultHttpPort};
    std::array<char, kConfigUrlCapacity> configUrl{"http://127.0.0.1:30975/config/"};
    std::array<PortRoute, kPortRouteCapacity> portRoutes{};
    std::size_t portRouteCount{};
};

} // namespace dawn::core::settings::client::external
