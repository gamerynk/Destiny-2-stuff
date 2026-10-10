#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::client::content::investment::layout {

inline constexpr std::size_t kGlobalsRootTagOffset = 16;
inline constexpr std::size_t kInventoryBucketTableTagOffset = 280;
inline constexpr std::size_t kItemTableTagOffset = 776;
inline constexpr std::size_t kSocketEntryListTableTagOffset = 1560;

struct InvestmentGlobals {
    std::array<std::byte, kGlobalsRootTagOffset> opaqueBeforeInvestmentRootTag{};
    std::uint32_t investmentRootTag{};
};

struct InvestmentRoot {
    std::array<std::byte, kInventoryBucketTableTagOffset> opaqueBeforeInventoryBucketTableTag{};
    std::uint32_t inventoryBucketTableTag{};
    std::array<std::byte,
               kItemTableTagOffset - kInventoryBucketTableTagOffset - sizeof(std::uint32_t)>
        opaqueBeforeItemTableTag{};
    std::uint32_t itemTableTag{};
    std::array<std::byte,
               kSocketEntryListTableTagOffset - kItemTableTagOffset - sizeof(std::uint32_t)>
        opaqueBeforeSocketEntryListTableTag{};
    std::uint32_t socketEntryListTableTag{};
};

static_assert(offsetof(InvestmentGlobals, investmentRootTag) == kGlobalsRootTagOffset);
static_assert(offsetof(InvestmentRoot, inventoryBucketTableTag) == kInventoryBucketTableTagOffset);
static_assert(offsetof(InvestmentRoot, itemTableTag) == kItemTableTagOffset);
static_assert(offsetof(InvestmentRoot, socketEntryListTableTag) == kSocketEntryListTableTagOffset);

} // namespace dawn::client::content::investment::layout
