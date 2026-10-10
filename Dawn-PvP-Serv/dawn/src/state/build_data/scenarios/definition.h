#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::scenarios {

inline constexpr std::size_t kDefinitionCapacity = 512;
inline constexpr std::size_t kBubbleCapacity = 64;
inline constexpr std::size_t kNameCapacity = 40;
inline constexpr std::uint8_t kBubbleEnabledByte = 0x80;
inline constexpr std::uint8_t kBubbleDisabledByte = 0x7F;
inline constexpr std::size_t kRosterGroupCapacity = 128;
inline constexpr std::size_t kRosterSlotCapacity = 1280;
inline constexpr std::size_t kDestinationGroupCapacity = 4;
inline constexpr std::size_t kDestinationBubbleGroupCapacity = 4;
inline constexpr std::size_t kBubbleMaskBytes = kBubbleCapacity / 8;
inline constexpr std::uint8_t kSlotSenseFlag = 1;
inline constexpr std::uint8_t kSlotAuthFlag = 2;
inline constexpr std::uint8_t kSlotFlagMask = kSlotSenseFlag | kSlotAuthFlag;
inline constexpr std::uint8_t kMaximumSlotType = 72;
inline constexpr std::size_t kSpawnStemCapacity = 32;
inline constexpr std::size_t kDestinationPackageCapacity = 8;

struct RosterGroup {
    std::uint32_t registryKey{};
    std::uint32_t objectTag{};
    std::uint16_t slotCount{};
    std::array<std::uint8_t, kRosterSlotCapacity> slotTypes{};
    std::array<std::uint8_t, kRosterSlotCapacity> slotFlags{};
    std::array<std::uint16_t, kRosterSlotCapacity> slotIndices{};
};

struct Definition {
    std::array<char, kNameCapacity> name{};
    std::uint32_t tag{};
    std::uint8_t nameLength{};
    std::uint8_t bubbleCount{};
    std::uint8_t truncated{};
    // a key the current slice set cannot find crashes the client's teardown sweep, so a group
    // present in only some slice sets is dropped, not published
    std::uint8_t rosterGroupCount{};
    std::uint8_t bubbleGroupCount{};
    std::uint8_t spawnStemLength{};
    std::array<char, kSpawnStemCapacity> spawnStem{};
    std::array<std::uint16_t, kDestinationGroupCapacity> rosterGroups{};
    std::array<std::uint16_t, kDestinationBubbleGroupCapacity> bubbleGroups{};
    std::array<std::uint64_t, kDestinationBubbleGroupCapacity> bubbleGroupMasks{};
    // every byte comes from the scenario blob alone, so one tag read per destination builds the domain
    std::array<std::uint8_t, kBubbleCapacity> bubbleStates{};
    std::array<std::uint32_t, kBubbleCapacity> bubbleHashes{};
    std::array<std::uint8_t, kBubbleCapacity> bubbleStateCounts{};
    std::array<std::uint16_t, kBubbleCapacity> bubbleMapIndices{};
    // a spawn set in an activity package outside this list is not streamed, however well the mask fits
    std::uint8_t packageCount{};
    std::array<std::uint16_t, kDestinationPackageCapacity> packages{};
};

} // namespace dawn::state::build_data::scenarios
