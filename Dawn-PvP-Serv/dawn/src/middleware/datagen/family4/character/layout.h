#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../inventory/layout.h"
#include "../progression/layout.h"

namespace dawn::middleware::datagen::family4::character::layout {

inline constexpr std::size_t kInventoryCapacity = 350;
inline constexpr std::size_t kEquipmentCapacity = 20;
inline constexpr std::size_t kSeenMessageByteCount = 16;
inline constexpr std::size_t kRosterMirrorCapacity = 20;
inline constexpr std::size_t kRosterMirrorEntrySize = 144;
inline constexpr std::size_t kNewItemFlagByteCount = 44;
inline constexpr std::size_t kInstanceProgressWatermarkCapacity = kInventoryCapacity;
inline constexpr std::size_t kProgressionCapacity = 127;
inline constexpr std::size_t kPreviewMirrorCount = 3;
inline constexpr std::size_t kIdentityValueCount = 3;
inline constexpr std::size_t kFlagCapacity = 4096;
inline constexpr std::size_t kObjectiveValueCapacity = 768;
// compact creation header; stays 0 for an existing character
inline constexpr std::size_t kCreationHeaderSize = 36;
inline constexpr std::size_t kInventorySerialPaddingSize = 4;
inline constexpr std::size_t kInventoryChangeUnknownSize = 4;
inline constexpr std::size_t kInventoryChangeRecordCapacity = 16;
inline constexpr std::size_t kSummaryGatePaddingSize = 52;
inline constexpr std::size_t kGateStatePaddingSize = 14;
inline constexpr std::size_t kGateSeenPaddingSize = 16;
inline constexpr std::size_t kSeenRosterPaddingSize = 28;
inline constexpr std::size_t kRosterDestinationPaddingSize = 176;
inline constexpr std::size_t kProgressPreviewPaddingSize = 792;
inline constexpr std::size_t kPreviewProgressionPaddingSize = 5;
inline constexpr std::size_t kPeriodicResetRecordSize = 56;
inline constexpr std::size_t kResetFlagsPaddingSize = 18224;
inline constexpr std::size_t kValuesContentPaddingSize = 1464;
inline constexpr std::size_t kContentTailPaddingSize = 591;
inline constexpr std::size_t kObjectSize = 46928;
inline constexpr std::size_t kSummaryIntegerCount = 3;
inline constexpr std::size_t kSummaryScalarCount = 2;
inline constexpr std::size_t kSummaryDefinitionWordCount = 2;
inline constexpr std::size_t kSummaryArrayCount = 2;
inline constexpr std::uint16_t kEmptySummaryDefinitionIndex = 0xFFFF;

#pragma pack(push, 1)

struct Identity {
    std::int8_t race{};
    std::int8_t gender{};
    std::int8_t characterClass{};
    std::uint8_t reserved{};
};

struct EquipmentSummaryEntry {
    std::uint16_t definitionIndex{};
    std::uint16_t reserved{};
    std::int32_t score{};
};

// stored summary is consumed before any live inventory recomputation
struct EquipmentSummary {
    std::array<EquipmentSummaryEntry, kEquipmentCapacity> profile{};
    std::array<EquipmentSummaryEntry, kEquipmentCapacity> character{};
    std::int32_t divisor{};
    std::int32_t total{};
    std::int32_t light{};
    float lightScalar{};
    float damageScalar{};
};

struct RosterMirrorEntry {
    std::array<std::byte, kRosterMirrorEntrySize> bytes{};
};

struct InventoryChangeRecord {
    // rising identity; the native observer prefers the greatest matching value
    std::uint16_t sequence{};
    std::uint16_t reserved{};
    std::int32_t mutationSerial{};
    std::uint8_t kind{};
    std::uint8_t reservedKind{};
    std::uint16_t flags{};
};

struct InventoryChangeList {
    std::uint16_t writeSlot{};
    std::uint16_t nextSequence{};
    std::array<InventoryChangeRecord, kInventoryChangeRecordCapacity> records{};
};

struct Object {
    std::uint64_t characterSoid{};
    Identity identity{};
    std::array<std::byte, kCreationHeaderSize> creationHeader{};
    // next serial for a newly mutated row, not an occupancy count
    std::uint32_t nextInventorySerial{};
    std::array<std::byte, kInventorySerialPaddingSize> inventorySerialPadding{};
    std::array<inventory::layout::Entry, kInventoryCapacity> inventoryItems{};
    std::array<std::byte, kInventoryChangeUnknownSize> inventoryChangeUnknown{};
    InventoryChangeList inventoryChanges{};
    std::array<std::uint64_t, kEquipmentCapacity> equippedInstanceSoids{};
    EquipmentSummary equipmentSummary{};
    std::array<std::byte, kSummaryGatePaddingSize> summaryGatePadding{};
    // 0 is a valid definition index and keeps the inventory-present gate open
    std::uint16_t inventoryGateDefinitionIndex{};
    std::array<std::byte, kGateStatePaddingSize> gateStatePadding{};
    // 0 is the required starting gate state
    std::uint32_t inventoryGateState{};
    std::array<std::byte, kGateSeenPaddingSize> gateSeenPadding{};
    std::array<std::byte, kSeenMessageByteCount> seenMessages{};
    std::array<std::byte, kSeenRosterPaddingSize> seenRosterPadding{};
    std::array<RosterMirrorEntry, kRosterMirrorCapacity> rosterMirror{};
    std::array<std::byte, kRosterDestinationPaddingSize> rosterDestinationPadding{};
    std::uint32_t lastOrbitedDestination{};
    std::array<std::byte, kNewItemFlagByteCount> newItemFlags{};
    std::array<std::int32_t, kInstanceProgressWatermarkCapacity> instanceProgressWatermarks{};
    std::array<std::byte, kProgressPreviewPaddingSize> progressPreviewPadding{};
    std::array<std::uint8_t, kPreviewMirrorCount> previewMirrors{};
    std::array<std::byte, kPreviewProgressionPaddingSize> previewProgressionPadding{};
    std::array<progression::layout::Entry, kProgressionCapacity> progressions{};
    std::array<std::byte, kPeriodicResetRecordSize> periodicResetRecord{};
    std::array<std::byte, kResetFlagsPaddingSize> resetFlagsPadding{};
    std::array<std::byte, kFlagCapacity> acquiredFlags{};
    std::array<std::int32_t, kObjectiveValueCapacity> objectiveValues{};
    std::array<std::byte, kValuesContentPaddingSize> valuesContentPadding{};
    // effective only with the matching family-five gate arm
    std::uint8_t contentBypass{};
    std::array<std::byte, kContentTailPaddingSize> contentTailPadding{};
};

#pragma pack(pop)

static_assert(sizeof(Identity) == kIdentityValueCount * sizeof(std::int8_t) + sizeof(std::uint8_t));
static_assert(sizeof(EquipmentSummaryEntry)
              == kSummaryDefinitionWordCount * sizeof(std::uint16_t) + sizeof(std::int32_t));
static_assert(sizeof(EquipmentSummary)
              == kSummaryArrayCount * kEquipmentCapacity * sizeof(EquipmentSummaryEntry)
                     + kSummaryIntegerCount * sizeof(std::int32_t)
                     + kSummaryScalarCount * sizeof(float));
static_assert(sizeof(InventoryChangeRecord)
              == 3 * sizeof(std::uint16_t) + sizeof(std::int32_t) + 2 * sizeof(std::uint8_t));
static_assert(offsetof(InventoryChangeRecord, mutationSerial) == 2 * sizeof(std::uint16_t));
static_assert(offsetof(InventoryChangeRecord, kind)
              == 2 * sizeof(std::uint16_t) + sizeof(std::int32_t));
static_assert(offsetof(InventoryChangeRecord, flags)
              == 2 * sizeof(std::uint16_t) + sizeof(std::int32_t) + 2 * sizeof(std::uint8_t));
static_assert(sizeof(InventoryChangeList)
              == 2 * sizeof(std::uint16_t)
                     + kInventoryChangeRecordCapacity * sizeof(InventoryChangeRecord));
static_assert(offsetof(InventoryChangeList, records) == 2 * sizeof(std::uint16_t));
static_assert(sizeof(Object) == kObjectSize);
static_assert(std::is_trivially_copyable_v<Object>);

} // namespace dawn::middleware::datagen::family4::character::layout
