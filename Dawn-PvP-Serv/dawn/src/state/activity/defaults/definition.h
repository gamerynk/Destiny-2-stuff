#pragma once

#include <cstddef>
#include <cstdint>

#include "../destination/definition.h"

namespace dawn::state::activity::defaults {

inline constexpr std::size_t kBubbleCapacity = 64;
inline constexpr std::size_t kSliceStatesPerBubble = 8;
inline constexpr std::uint8_t kMinimumBubbleCount = 1;
inline constexpr std::uint16_t kMaximumInitialSliceSet = 511;

struct FallbackPolicy final {
    std::uint8_t bubbleCount{};
    std::uint64_t statefulBubbleMask{};
    std::uint16_t initialSliceSet{};
    std::uint32_t spawnSetHash{};
};

struct DefaultDestination final {
    destination::DestinationSelection selection{};
    FallbackPolicy fallback{};
};

// a few maps bind their arrival at map load instead of in the packages, so it cannot be
// derived; the reference set is 20 rows
inline constexpr std::size_t kArrivalOverrideCapacity = 64;

struct ArrivalOverride final {
    std::array<char, destination::kPackageNameCapacity> name{};
    std::uint8_t nameLength{};
    std::uint8_t bubble{};
    bool hasBubble{};
    std::uint32_t spawnSetHash{};
    bool hasSpawnSetHash{};
};

struct ActivityDefaults final {
    DefaultDestination defaultDestination{};
    std::array<ArrivalOverride, kArrivalOverrideCapacity> arrivalOverrides{};
    std::uint8_t arrivalOverrideCount{};
    // send the membership identity's field3 as message 5's player key (member record +16),
    // not the character SOID
    bool rosterKeyFromIdentity{};
    // fill message 5's participation body on every type-13 slot of the key group; the gate reads
    // whichever object the player datum names, which need not be the first
    bool rosterKeyOnAllSlots{};
    bool competitiveTeams{};
    bool competitiveScoreboard{};
    bool crucibleControlPlacementTest{};
};

} // namespace dawn::state::activity::defaults
