#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace dawn::state::build_data::inventory::buckets {

enum class ArraySelector : std::uint8_t {
    character = 0,
    profile = 1,
    smallProfile = 2,
};

inline constexpr std::uint32_t kCharacterSlotCapacity = 350;
inline constexpr std::uint32_t kProfileSlotCapacity = 701;
inline constexpr std::uint32_t kSmallProfileSlotCapacity = 6;
inline constexpr std::uint8_t kUnavailableBucketId = (std::numeric_limits<std::uint8_t>::max)();
inline constexpr std::size_t kDescriptorCapacity = kUnavailableBucketId;
inline constexpr std::int8_t kUnavailableEquipmentSlot = -1;
inline constexpr std::size_t kDescriptorByteSize = 8;

struct Descriptor {
    std::uint8_t bucketId{};
    ArraySelector arraySelector{};
    std::uint16_t firstSlot{};
    std::uint16_t slotCount{};
    std::int8_t equipmentSlot{kUnavailableEquipmentSlot};
    std::uint8_t reserved{};
};

static_assert(sizeof(Descriptor) == kDescriptorByteSize);
static_assert(std::is_trivially_copyable_v<Descriptor>);

} // namespace dawn::state::build_data::inventory::buckets
