#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include "../crucible/menu_rules.h"

namespace dawn::state::activity::destination {

inline constexpr std::size_t kPackageNameCapacity = 40;
inline constexpr std::int8_t kMinimumReason = -1;
inline constexpr std::int8_t kMaximumReason = 14;
inline constexpr std::int16_t kAbsentActivityIndex = -1;
inline constexpr std::int16_t kMaximumActivityIndex = 4'094;
inline constexpr std::int16_t kAbsentElementIndex = -1;
inline constexpr std::int16_t kMaximumElementIndex = 510;
inline constexpr std::uint32_t kAbsentSpawnSetHash = 0x811C9DC5U;
// exact descriptor bits replayed verbatim by one outbound message; widest 1,814 bits, common 372
inline constexpr std::size_t kDescriptorCapacity = 232;

struct DestinationSelection final {
    friend bool operator==(const DestinationSelection&,const DestinationSelection&)=default;
    std::array<crucible::menu_rules::Selection,63> menuOptions{};
    std::uint8_t menuOptionCount{};
    std::array<std::int8_t, kPackageNameCapacity> packageName{};
    std::uint8_t packageNameLength{};
    std::int8_t reason{kMinimumReason};
    std::int16_t previousActivityIndex{kAbsentActivityIndex};
    std::int16_t activityIndex{};
    std::int16_t elementIndex{kAbsentElementIndex};
    std::uint32_t arrivalBubbleHash{};
    std::uint32_t spawnSetHash{};
    bool hasElementIndex{};
    bool hasArrivalBubbleHash{};
    bool hasSpawnSetHash{};
    // a few destinations bind their arrival at map load, so it is authored per name
    std::uint8_t arrivalBubbleOverride{};
    bool hasArrivalBubbleOverride{};
    // a bubble can declare several slice sets, so naming the bubble cannot reach past its first
    std::uint16_t sliceSetOverride{};
    bool hasSliceSetOverride{};
    std::uint32_t spawnSetOverride{};
    bool hasSpawnSetOverride{};
    // the descriptor's own bits as the client encoded them; it has fields with no known name,
    // so re-encoding from the scalars would drop them
    std::array<std::byte, kDescriptorCapacity> descriptorBits{};
    std::uint16_t descriptorBitLength{};
    std::uint16_t descriptorNameBit{};
    bool hasDescriptorName{};
};

[[nodiscard]] constexpr bool usable_spawn_set_hash(bool present, std::uint32_t value) noexcept {
    return present && value != 0 && value != kAbsentSpawnSetHash;
}

[[nodiscard]] constexpr std::uint32_t resolve_spawn_set_hash(const DestinationSelection& selection,
                                                             std::uint32_t fallback) noexcept {
    if (selection.hasSpawnSetOverride) {
        return selection.spawnSetOverride;
    }
    return usable_spawn_set_hash(selection.hasSpawnSetHash, selection.spawnSetHash)
               ? selection.spawnSetHash
               : fallback;
}

} // namespace dawn::state::activity::destination
