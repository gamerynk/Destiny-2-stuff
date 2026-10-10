#pragma once

#include <cstddef>
#include <type_traits>

#include "layout.h"

namespace dawn::middleware::datagen::family4::character::abi {

inline constexpr std::size_t kCharacterSoidOffset = 0;
inline constexpr std::size_t kIdentityOffset = 8;
inline constexpr std::size_t kNextInventorySerialOffset = 48;
inline constexpr std::size_t kInventoryItemsOffset = 56;
inline constexpr std::size_t kInventoryChangeUnknownOffset = 11256;
inline constexpr std::size_t kInventoryChangeHeaderOffset = 11260;
inline constexpr std::size_t kInventoryChangeRecordsOffset = 11264;
inline constexpr std::size_t kEquippedInstancesOffset = 11456;
inline constexpr std::size_t kEquipmentSummaryOffset = 11616;
inline constexpr std::size_t kInventoryGateDefinitionOffset = 12008;
inline constexpr std::size_t kInventoryGateStateOffset = 12024;
inline constexpr std::size_t kSeenMessagesOffset = 12044;
inline constexpr std::size_t kRosterMirrorOffset = 12088;
inline constexpr std::size_t kLastOrbitDestinationOffset = 15144;
inline constexpr std::size_t kNewItemFlagsOffset = 15148;
inline constexpr std::size_t kInstanceProgressWatermarksOffset = 15192;
inline constexpr std::size_t kPreviewMirrorsOffset = 17384;
inline constexpr std::size_t kProgressionsOffset = 17392;
inline constexpr std::size_t kAcquiredFlagsOffset = 37704;
inline constexpr std::size_t kObjectiveValuesOffset = 41800;
inline constexpr std::size_t kContentBypassOffset = 46336;

static_assert(std::is_standard_layout_v<layout::Object>);
static_assert(offsetof(layout::Object, characterSoid) == kCharacterSoidOffset);
static_assert(offsetof(layout::Object, identity) == kIdentityOffset);
static_assert(offsetof(layout::Object, nextInventorySerial) == kNextInventorySerialOffset);
static_assert(offsetof(layout::Object, inventoryItems) == kInventoryItemsOffset);
static_assert(offsetof(layout::Object, inventoryChangeUnknown) == kInventoryChangeUnknownOffset);
static_assert(offsetof(layout::Object, inventoryChanges) == kInventoryChangeHeaderOffset);
static_assert(offsetof(layout::InventoryChangeList, records)
                  + offsetof(layout::Object, inventoryChanges)
              == kInventoryChangeRecordsOffset);
static_assert(offsetof(layout::Object, equippedInstanceSoids) == kEquippedInstancesOffset);
static_assert(offsetof(layout::Object, equipmentSummary) == kEquipmentSummaryOffset);
static_assert(offsetof(layout::Object, inventoryGateDefinitionIndex)
              == kInventoryGateDefinitionOffset);
static_assert(offsetof(layout::Object, inventoryGateState) == kInventoryGateStateOffset);
static_assert(offsetof(layout::Object, seenMessages) == kSeenMessagesOffset);
static_assert(offsetof(layout::Object, rosterMirror) == kRosterMirrorOffset);
static_assert(offsetof(layout::Object, lastOrbitedDestination) == kLastOrbitDestinationOffset);
static_assert(offsetof(layout::Object, newItemFlags) == kNewItemFlagsOffset);
static_assert(offsetof(layout::Object, instanceProgressWatermarks)
              == kInstanceProgressWatermarksOffset);
static_assert(offsetof(layout::Object, previewMirrors) == kPreviewMirrorsOffset);
static_assert(offsetof(layout::Object, progressions) == kProgressionsOffset);
static_assert(offsetof(layout::Object, acquiredFlags) == kAcquiredFlagsOffset);
static_assert(offsetof(layout::Object, objectiveValues) == kObjectiveValuesOffset);
static_assert(offsetof(layout::Object, contentBypass) == kContentBypassOffset);

} // namespace dawn::middleware::datagen::family4::character::abi
