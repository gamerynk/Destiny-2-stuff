#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "scenario_reader.h"

namespace dawn::middleware::content::packages::tables {

// activity msg 12 publishes this many region records
inline constexpr std::size_t kRegionRecordCount = 64;
// 64 bubbles spaced by the slice-set factor fill this region space
inline constexpr std::uint32_t kRegionIndexBound = 512;
inline constexpr std::size_t kRegionNameCapacity = 16;

struct RegionName {
    std::array<char, kRegionNameCapacity> chars{};
    std::size_t length{};
};

[[nodiscard]] constexpr std::uint32_t region_index(std::uint32_t bubbleIndex) noexcept {
    return bubbleIndex * kSliceSetIndexFactor;
}

[[nodiscard]] bool
region_name(std::uint32_t regionIndex, bool publicBubble, RegionName& output) noexcept;

static_assert(region_index(0) == 0);
static_assert(region_index(1) == 8);
static_assert(region_index(13) == 104);
static_assert(region_index(63) == 504);
static_assert(region_index(64) == kRegionIndexBound);

} // namespace dawn::middleware::content::packages::tables
