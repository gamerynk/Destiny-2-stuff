#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include "../../../middleware/content/social_activity_policy.h"

namespace dawn::client::hooks::bootflow::tower_search_completion_policy {

constexpr std::size_t kConfigSize = 0x388;
constexpr std::size_t kPropertiesSize = 0x2D0;
constexpr std::size_t kSearchPrefixSize = 0x68;
constexpr std::uint32_t kTowerActivity = 0xE8ABA41BU;

template <typename T>
[[nodiscard]] inline T read(std::span<const std::byte> data, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof value);
    return value;
}

// PC 1.59: EE91C0 can finish a zero-window composition while 1816630 awaits its first
// search/QoS result. Search+10 is a version, not a lifecycle state; keep it pending
[[nodiscard]] inline bool completion_ready(bool nativeReady,
                                           std::span<const std::byte> config,
                                           std::span<const std::byte> props,
                                           std::span<const std::byte> search) noexcept {
    if (!nativeReady) {
        return false;
    }
    if (config.size() != kConfigSize || props.size() != kPropertiesSize
        || search.size() != kSearchPrefixSize) {
        return nativeReady;
    }
    if (config[0] != std::byte{1}
        || !middleware::content::social_activity_policy::find(read<std::uint32_t>(config,0x1F8))
        || read<std::uint32_t>(props,8) != read<std::uint32_t>(config,0x1F8)
        || (std::to_integer<unsigned>(props[0x2A2]) & 3U) != 1U
        || read<std::uint64_t>(config, 0x220) != read<std::uint64_t>(props, 0)
        || read<std::uint32_t>(config, 0x21C) != read<std::uint32_t>(props, 0xC)
        || read<std::int32_t>(config, 0x248) != 0) {
        return nativeReady;
    }
    const auto capacity = read<std::int32_t>(config, 0x28C);
    const auto count = read<std::int32_t>(props, 0x28);
    if (capacity <= 0 || count <= 0 || count >= capacity) {
        return nativeReady;
    }
    if (read<std::uint32_t>(search, 0) != 0) {
        return nativeReady;
    }
    const auto flags = read<std::uint16_t>(search, 4);
    if ((flags & 0x20U) != 0) {
        return false;
    }
    const auto epoch = read<std::int32_t>(search, 0x14);
    return epoch < 0 || epoch >= 16 || (flags & (0x04U | 0x10U)) == 0;
}

} // namespace dawn::client::hooks::bootflow::tower_search_completion_policy
