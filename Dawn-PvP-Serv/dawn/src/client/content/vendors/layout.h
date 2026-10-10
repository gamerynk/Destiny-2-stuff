#pragma once

#include <cstddef>
#include <cstdint>

namespace dawn::client::content::vendors {

inline constexpr std::uint32_t kIndexRootTag = 0x8131931DU;

inline constexpr std::size_t kInstalledArrayDescriptor = 32;
inline constexpr std::size_t kSaleArrayDescriptor = 48;
inline constexpr std::size_t kThirdArrayDescriptor = 80;
// raw; unit, epoch and scope are not known, so it is stored unconverted
inline constexpr std::size_t kResetIntervalOffset = 20;
inline constexpr std::size_t kResetPhaseOffset = 24;

inline constexpr std::size_t kSaleExpression8Offset = 8;
inline constexpr std::size_t kSaleNestedRecordOffset = 32;
inline constexpr std::size_t kSaleItemIndexOffset = 70;
inline constexpr std::size_t kSaleCategoryIndexOffset = 100;
inline constexpr std::size_t kSaleRaw104Offset = 104;
inline constexpr std::size_t kSaleRaw108Offset = 108;
inline constexpr std::size_t kSaleExpression120Offset = 120;
inline constexpr std::size_t kSaleCount136Offset = 136;
inline constexpr std::size_t kSaleFeatureBranchOffset = 154;
inline constexpr std::size_t kSaleExpression160Offset = 160;
inline constexpr std::size_t kSaleRaw172Offset = 172;
inline constexpr std::size_t kSaleSecondaryItemOffset = 176;

} // namespace dawn::client::content::vendors
