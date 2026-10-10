#pragma once

#include <cstddef>
#include <type_traits>

#include "layout.h"

namespace dawn::middleware::datagen::family4::instance::abi {

inline constexpr std::size_t kInstanceSoidOffset = 0;
inline constexpr std::size_t kLevelOffset = 8;
inline constexpr std::size_t kLevelCurveOffset = 12;
inline constexpr std::size_t kLevelCapRowOffset = 14;
inline constexpr std::size_t kBaseDefinitionOffset = 16;
inline constexpr std::size_t kOrdinarySocketsOffset = 20;
inline constexpr std::size_t kActiveMaskOffset = 164;
inline constexpr std::size_t kBlockedMaskOffset = 172;
inline constexpr std::size_t kGateMaskOffset = 180;
inline constexpr std::size_t kRollStateOffset = 184;
inline constexpr std::size_t kProgressionDefinitionOffset = 196;
inline constexpr std::size_t kSelectorsOffset = 200;
inline constexpr std::size_t kSocketEntryListOffset = 236;
inline constexpr std::size_t kRandomRollOffset = 238;
inline constexpr std::size_t kSocketEntryStatesOffset = 247;
inline constexpr std::size_t kCreationRequestOffset = 284;
inline constexpr std::size_t kCreationEntryCountOffset = 304;
inline constexpr std::size_t kCreationEntriesOffset = 308;
inline constexpr std::size_t kTailDefinitionOffset = 380;
inline constexpr std::size_t kTailValuesOffset = 384;

static_assert(std::is_standard_layout_v<layout::Object>);
static_assert(offsetof(layout::Object, instanceSoid) == kInstanceSoidOffset);
static_assert(offsetof(layout::Object, level) == kLevelOffset);
static_assert(offsetof(layout::Object, level) + offsetof(layout::LevelState, curveX)
              == kLevelCurveOffset);
static_assert(offsetof(layout::Object, level) + offsetof(layout::LevelState, capRow)
              == kLevelCapRowOffset);
static_assert(offsetof(layout::Object, baseDefinitionIndex) == kBaseDefinitionOffset);
static_assert(offsetof(layout::Object, ordinarySockets) == kOrdinarySocketsOffset);
static_assert(offsetof(layout::Object, ordinarySockets)
                  + offsetof(layout::OrdinarySocketBlock, activeMask)
              == kActiveMaskOffset);
static_assert(offsetof(layout::Object, ordinarySockets)
                  + offsetof(layout::OrdinarySocketBlock, blockedMask)
              == kBlockedMaskOffset);
static_assert(offsetof(layout::Object, ordinarySockets)
                  + offsetof(layout::OrdinarySocketBlock, gateMask)
              == kGateMaskOffset);
static_assert(offsetof(layout::Object, roll) == kRollStateOffset);
static_assert(offsetof(layout::Object, roll)
                  + offsetof(layout::RollState, progressionDefinitionIndex)
              == kProgressionDefinitionOffset);
static_assert(offsetof(layout::Object, roll) + offsetof(layout::RollState, selectors)
              == kSelectorsOffset);
static_assert(offsetof(layout::Object, roll) + offsetof(layout::RollState, socketEntryListIndex)
              == kSocketEntryListOffset);
static_assert(offsetof(layout::Object, roll) + offsetof(layout::RollState, randomRoll)
              == kRandomRollOffset);
static_assert(offsetof(layout::Object, roll) + offsetof(layout::RollState, socketEntryStates)
              == kSocketEntryStatesOffset);
static_assert(offsetof(layout::Object, creation) == kCreationRequestOffset);
static_assert(offsetof(layout::Object, creation) + offsetof(layout::CreationRequest, entryCount)
              == kCreationEntryCountOffset);
static_assert(offsetof(layout::Object, creation) + offsetof(layout::CreationRequest, entries)
              == kCreationEntriesOffset);
static_assert(offsetof(layout::Object, tailDefinitionIndex) == kTailDefinitionOffset);
static_assert(offsetof(layout::Object, tailValues) == kTailValuesOffset);

} // namespace dawn::middleware::datagen::family4::instance::abi
