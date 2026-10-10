#pragma once

#include <cstdint>

namespace dawn::client::content::scenarios::host_slots {

[[nodiscard]] constexpr bool crucible_group(std::uint32_t key) noexcept {
    return key == 0x39774EE1U || key == 0xD28FF232U;
}

// host-only slot families per installed descriptors and the component registry: declared
// ordinal but no client component, sense reset or auth body. Never invent flags for others
[[nodiscard]] constexpr bool crucible_host_slot(std::uint32_t key,
                                                std::uint32_t type) noexcept {
    return crucible_group(key) && (type == 47 || type == 60 || type == 61);
}

} // namespace dawn::client::content::scenarios::host_slots
