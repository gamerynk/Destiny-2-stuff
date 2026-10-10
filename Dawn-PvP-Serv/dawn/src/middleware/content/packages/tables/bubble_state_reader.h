#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "scenario_reader.h"

namespace dawn::middleware::content::packages::tables {

// msg 1 carries one byte per bubble; 64 fits the 512-wide region space exactly
inline constexpr std::size_t kBubbleStateCapacity = 64;
// byte for a bubble whose first slice-set state is enabled; others get 0x7F
inline constexpr std::uint8_t kBubbleEnabledByte = 0x80;
inline constexpr std::uint8_t kBubbleDisabledByte = 0x7F;
inline constexpr std::uint64_t kBubbleStateCountCeiling = 255;
inline constexpr std::size_t kScenarioPackageCapacity = 8;

static_assert(kBubbleStateCapacity * kSliceSetIndexFactor == 512);

struct BubbleStates {
    std::array<std::uint8_t, kBubbleStateCapacity> bytes{};
    // client names its arrival bubble by hash; this maps the hash to an index
    std::array<std::uint32_t, kBubbleStateCapacity> hashes{};
    // a bubble owns a run of slice sets starting at its region index; this is the run length
    std::array<std::uint8_t, kBubbleStateCapacity> stateCounts{};
    // container bubble masks are keyed by this index
    std::array<std::uint16_t, kBubbleStateCapacity> mapIndices{};
    // packages this destination loads; spawn sets from other packages are not streamed
    std::array<std::uint16_t, kScenarioPackageCapacity> packages{};
    std::size_t packageCount{};
    std::size_t count{};
    bool truncated{};
};

[[nodiscard]] bool bubble_states(std::span<const std::byte> scenario,
                                 BubbleStates& output) noexcept;

} // namespace dawn::middleware::content::packages::tables
