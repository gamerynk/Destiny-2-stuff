#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../../../state/account/inventory/inventory_state.h"
#include "../instance/instance_encoder.h"

namespace dawn::middleware::datagen::family4::loadout {

inline constexpr std::size_t kItemCapacity = state::account::inventory::kEquipmentSlotCount
                                             + state::account::inventory::kCharacterItemCapacity;

struct ResolvedItem {
    std::uint16_t inventoryRow{};
    std::uint8_t equipmentSlot{};
    // only equipped items publish their instance SOID into the native equipment array
    bool equipped{};
    std::int32_t quantity{};
    std::int32_t mutationSerial{};
    std::uint32_t flags{};
    instance::ResolvedInstance instance{};
};

struct SlottedInstance {
    std::uint8_t equipmentSlot{};
    instance::ResolvedInstance instance{};
};

struct ResolvedInstances {
    std::array<SlottedInstance, kItemCapacity> items{};
    std::size_t itemCount{};
};

struct ResolvedLoadout {
    std::array<ResolvedItem, kItemCapacity> items{};
    std::size_t itemCount{};
    std::uint32_t nextInventorySerial{};
};

} // namespace dawn::middleware::datagen::family4::loadout
