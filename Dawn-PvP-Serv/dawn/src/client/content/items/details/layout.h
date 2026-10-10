#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::client::content::items::details::layout {

inline constexpr std::size_t kEquipmentBlockRelativeOffset = 16;
inline constexpr std::size_t kOrdinarySocketBlockRelativeOffset = 104;
inline constexpr std::size_t kSocketListBlockRelativeOffset = 128;
inline constexpr std::size_t kMaxStackSizeOffset = 180;
inline constexpr std::size_t kBucketIdOffset = 184;
// definition byte 187: zero for stackable rows, nonzero for instanced rows
inline constexpr std::size_t kInstancedDefinitionPredicateOffset = 187;
inline constexpr std::size_t kEquipmentSlotOffset = 24;
inline constexpr std::size_t kArrayHeaderRelativeOffset = 8;
inline constexpr std::size_t kArrayMarkerBackOffset = sizeof(std::uint32_t);
inline constexpr std::uint32_t kArrayHeaderMarker = 0x80809FBDU;
inline constexpr std::uint32_t kOrdinarySocketElementClass = 0x808077C4U;
inline constexpr std::size_t kArrayFirstEntryOffset = 16;
inline constexpr std::size_t kOrdinarySocketEntrySize = 80;
inline constexpr std::size_t kInitialPlugIndexOffset = 2;

struct RelativeArray {
    std::uint64_t count{};
    std::int64_t headerRelative{};
};

struct ItemDefinition {
    std::array<std::byte, kEquipmentBlockRelativeOffset> opaqueBeforeEquipmentBlock{};
    std::int64_t equipmentBlockRelative{};
    std::array<std::byte,
               kOrdinarySocketBlockRelativeOffset - kEquipmentBlockRelativeOffset
                   - sizeof(std::int64_t)>
        opaqueBeforeOrdinarySocketBlock{};
    std::int64_t ordinarySocketBlockRelative{};
    // stats are read from the package blob, never loaded memory, so this stays opaque
    std::array<std::byte,
               kSocketListBlockRelativeOffset - kOrdinarySocketBlockRelativeOffset
                   - sizeof(std::int64_t)>
        opaqueBeforeSocketListBlock{};
    std::int64_t socketListBlockRelative{};
    std::array<std::byte,
               kMaxStackSizeOffset - kSocketListBlockRelativeOffset - sizeof(std::int64_t)>
        opaqueBeforeMaxStackSize{};
    std::int32_t maxStackSize{};
    std::uint8_t bucketId{};
    std::array<std::byte,
               kInstancedDefinitionPredicateOffset - kBucketIdOffset - sizeof(std::uint8_t)>
        opaqueBeforeInstancedDefinitionPredicate{};
    std::uint8_t instancedDefinitionPredicate{};
};

struct EquipmentBlock {
    std::array<std::byte, kEquipmentSlotOffset> opaqueBeforeSlot{};
    std::int8_t slot{};
};

struct OrdinarySocketEntry {
    std::uint16_t socketTypeIndex{};
    std::uint16_t initialPlugIndex{};
    std::array<std::byte,
               kOrdinarySocketEntrySize - kInitialPlugIndexOffset - sizeof(std::uint16_t)>
        opaqueAfterInitialPlug{};
};

struct ArrayHeader {
    std::uint64_t repeatedCount{};
    std::uint32_t elementClass{};
    std::array<std::byte, kArrayFirstEntryOffset - sizeof(std::uint64_t) - sizeof(std::uint32_t)>
        opaqueBeforeFirstEntry{};
};

struct SocketListBlock {
    std::uint16_t index{};
};

static_assert(offsetof(ItemDefinition, equipmentBlockRelative) == kEquipmentBlockRelativeOffset);
static_assert(offsetof(ItemDefinition, ordinarySocketBlockRelative)
              == kOrdinarySocketBlockRelativeOffset);
static_assert(offsetof(ItemDefinition, socketListBlockRelative) == kSocketListBlockRelativeOffset);
static_assert(offsetof(ItemDefinition, maxStackSize) == kMaxStackSizeOffset);
static_assert(offsetof(ItemDefinition, bucketId) == kBucketIdOffset);
static_assert(offsetof(ItemDefinition, instancedDefinitionPredicate)
              == kInstancedDefinitionPredicateOffset);
static_assert(offsetof(EquipmentBlock, slot) == kEquipmentSlotOffset);
static_assert(offsetof(RelativeArray, headerRelative) == kArrayHeaderRelativeOffset);
static_assert(offsetof(OrdinarySocketEntry, initialPlugIndex) == kInitialPlugIndexOffset);
static_assert(sizeof(OrdinarySocketEntry) == kOrdinarySocketEntrySize);
static_assert(sizeof(ArrayHeader) == kArrayFirstEntryOffset);

} // namespace dawn::client::content::items::details::layout
