#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "scenario_reader.h"

namespace dawn::middleware::content::packages::tables {

inline constexpr std::size_t kSliceSetCapacity = 64;
inline constexpr std::size_t kRosterKeyCapacity = 16;

static_assert(kSliceSetCapacity * kSliceSetIndexFactor == 512);
static_assert(kSliceSetCapacity == 64);

// client dereferences a miss unchecked, so a key missing from one slice set crashes on the switch out of it
struct RosterIntersection {
    std::array<std::uint32_t, kRosterKeyCapacity> keys{};
    std::array<std::uint64_t, kRosterKeyCapacity> masks{};
    std::size_t keyCount{};
    std::uint64_t observedSets{};
    bool overflowed{};
    bool unresolvedSet{};
};

void observe_unresolved_slice_set(RosterIntersection& state) noexcept;

// only 56 installed objects declare these; feeding every placed object overflows most destinations
inline constexpr std::array<std::uint16_t, 9> kRosterSlotTypes = {
    8, 13, 16, 17, 21, 35, 37, 41, 67};

[[nodiscard]] bool carries_roster_slot(std::span<const std::byte> object,
                                       std::uint32_t objectTag) noexcept;

// slice sets with no candidate still count, or an unsafe key looks safe
[[nodiscard]] bool observe_slice_set(RosterIntersection& state,
                                     std::uint32_t sliceSetIndex) noexcept;

[[nodiscard]] bool observe_roster_key(RosterIntersection& state,
                                      std::uint32_t sliceSetIndex,
                                      std::uint32_t objectKey) noexcept;

[[nodiscard]] bool safe_roster_keys(const RosterIntersection& state,
                                    std::span<std::uint32_t> output,
                                    std::size_t& count) noexcept;

// can't go in the top-level roster: client may dereference them while a slice without them is
// current. they belong in the per-bubble sub-blocks
[[nodiscard]] bool partial_roster_keys(const RosterIntersection& state,
                                       std::span<std::uint32_t> keys,
                                       std::span<std::uint64_t> masks,
                                       std::size_t& count) noexcept;

} // namespace dawn::middleware::content::packages::tables
