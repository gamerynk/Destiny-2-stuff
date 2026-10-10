#pragma once

#include <array>
#include <cstdint>

namespace dawn::state::activity::crucible {

struct PlacementFrame final {
    std::uint32_t registryKey{};
    std::uint32_t generation{};
    std::array<std::uint16_t, 3> zoneSlots{};
    bool enabled{};

    [[nodiscard]] bool owns(std::uint32_t key, std::uint8_t type,
                            std::uint16_t slot) const noexcept {
        if (!enabled || generation == 0 || generation >= 0x7FFFFFFFU
            || registryKey == 0 || registryKey == 0x811C9DC5U
            || key != registryKey || type != 4) return false;
        for (auto candidate : zoneSlots) if (slot == candidate) return true;
        return false;
    }
};

} // namespace dawn::state::activity::crucible
