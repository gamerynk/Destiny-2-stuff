#include "inventory_bucket_catalog.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <limits>

#include "../../table.h"

namespace dawn::state::build_data::inventory::buckets {
namespace {

constexpr std::uint16_t kEmptyLookupRow = (std::numeric_limits<std::uint16_t>::max)();

Lock g_lock;
Table<Descriptor, kDescriptorCapacity> g_descriptors;
std::array<std::uint16_t, kDescriptorCapacity> g_lookup{};

[[nodiscard]] bool selector_capacity(ArraySelector selector, std::uint32_t& capacity) noexcept {
    switch (selector) {
    case ArraySelector::character:
        capacity = kCharacterSlotCapacity;
        return true;
    case ArraySelector::profile:
        capacity = kProfileSlotCapacity;
        return true;
    case ArraySelector::smallProfile:
        capacity = kSmallProfileSlotCapacity;
        return true;
    }
    capacity = 0;
    return false;
}

[[nodiscard]] bool range_fits(const Descriptor& descriptor) noexcept {
    std::uint32_t capacity = 0;
    if (descriptor.slotCount == 0 || !selector_capacity(descriptor.arraySelector, capacity)) {
        return false;
    }
    const std::uint32_t firstSlot = descriptor.firstSlot;
    return firstSlot <= capacity && descriptor.slotCount <= capacity - firstSlot;
}

constexpr std::size_t kEquipmentSlotCount = 19;

[[nodiscard]] bool equipment_slot_fits(const Descriptor& descriptor) noexcept {
    return descriptor.equipmentSlot == kUnavailableEquipmentSlot
           || (descriptor.equipmentSlot >= 0
               && static_cast<std::size_t>(descriptor.equipmentSlot) < kEquipmentSlotCount);
}

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_descriptors.clear();
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
}

bool valid(std::span<const Descriptor> descriptors) noexcept {
    if (descriptors.empty() || descriptors.size() > kDescriptorCapacity) {
        return false;
    }

    std::array<bool, kDescriptorCapacity> occupied{};
    std::bitset<kEquipmentSlotCount> occupiedEquipmentSlots;
    bool hasEquipmentSlot = false;
    for (const Descriptor& descriptor : descriptors) {
        if (descriptor.bucketId == kUnavailableBucketId || occupied[descriptor.bucketId]
            || descriptor.reserved != 0 || !range_fits(descriptor)
            || !equipment_slot_fits(descriptor)) {
            return false;
        }
        if (descriptor.equipmentSlot != kUnavailableEquipmentSlot) {
            const std::size_t equipmentSlot = static_cast<std::size_t>(descriptor.equipmentSlot);
            if (occupiedEquipmentSlots.test(equipmentSlot)) {
                return false;
            }
            occupiedEquipmentSlots.set(equipmentSlot);
            hasEquipmentSlot = true;
        }
        occupied[descriptor.bucketId] = true;
    }
    return hasEquipmentSlot;
}

bool replace(std::span<const Descriptor> descriptors) noexcept {
    if (!valid(descriptors)) {
        return false;
    }

    const Lock::Exclusive guard(g_lock);
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
    if (!g_descriptors.replace(descriptors)) {
        return false;
    }
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        g_lookup[descriptors[index].bucketId] = static_cast<std::uint16_t>(index);
    }
    return true;
}

bool find(std::uint8_t bucketId, Descriptor& descriptor) noexcept {
    descriptor = {};
    if (bucketId == kUnavailableBucketId) {
        return false;
    }

    const Lock::Shared guard(g_lock);
    const std::span<const Descriptor> rows = g_descriptors.rows();
    const std::uint16_t row = g_lookup[bucketId];
    const bool found = row != kEmptyLookupRow && row < rows.size();
    if (found) {
        descriptor = rows[row];
    }
    return found;
}

bool snapshot(std::span<Descriptor> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_descriptors.snapshot(output, count);
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_descriptors.count();
}

} // namespace dawn::state::build_data::inventory::buckets
