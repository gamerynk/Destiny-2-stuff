#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::build_data::vendors {

inline constexpr std::size_t kIndexCapacity = 512;
inline constexpr std::size_t kDefinitionCapacity = 48;
inline constexpr std::size_t kSaleRowCapacity = 8192;
inline constexpr std::size_t kInstalledRowCapacity = 2048;

inline constexpr std::size_t kIndexRowStride = 24;
inline constexpr std::size_t kSaleRowStride = 184;
inline constexpr std::size_t kInstalledRowStride = 24;
inline constexpr std::size_t kThirdRowStride = 80;

inline constexpr std::uint32_t kIndexWrapperClass = 0x8080784AU;
inline constexpr std::uint32_t kIndexRowClass = 0x8080784EU;
inline constexpr std::uint32_t kDefinitionClass = 0x80807850U;
inline constexpr std::uint32_t kInstalledRowClass = 0x80807860U;
inline constexpr std::uint32_t kSaleRowClass = 0x80807861U;

inline constexpr std::uint16_t kAbsentSecondaryItem = 0xFFFFU;
// sale row +100 holds this when the row has no category; the client tests for it
inline constexpr std::int32_t kAbsentCategoryIndex = -1;

struct IndexEntry {
    std::uint32_t definitionHash{};
    std::uint32_t definitionTag{};
    // row position is the index the opcode-901 request carries
    std::uint16_t index{};
};

struct Definition {
    std::uint32_t definitionHash{};
    std::uint32_t definitionTag{};
    std::uint32_t definitionClass{};
    std::uint32_t definitionSize{};
    std::uint32_t installedRowBase{};
    std::uint32_t installedRowClass{};
    std::uint32_t saleRowBase{};
    std::uint32_t saleRowClass{};
    std::uint32_t thirdRowBase{};
    std::uint32_t thirdRowClass{};
    std::uint32_t saleRowOffset{};
    std::uint32_t installedRowOffset{};
    std::uint32_t resetIntervalRaw{};
    std::uint32_t resetPhaseRaw{};
    std::uint16_t index{};
    std::uint16_t installedCount{};
    std::uint16_t saleCount{};
    std::uint16_t thirdCount{};
};

// do not name a raw field a cost, award, stock or quantity without its mutation reader
struct SaleRow {
    std::uint16_t vendorIndex{};
    std::uint16_t rowIndex{};
    std::uint16_t itemIndex{};
    std::uint16_t secondaryItemIndex{};
    // row +100 is the vendor category, established by correlating 3,304 rows against the manifest
    std::int32_t categoryIndex{};
    std::uint32_t raw104{};
    std::uint32_t raw108{};
    // row +172: role unknown, and it is not the field the runtime selector reads
    std::int32_t raw172{};
    std::uint32_t expressionCount8{};
    std::uint32_t nestedRecordCount{};
    std::uint32_t expressionCount120{};
    std::uint32_t count136{};
    std::uint32_t expressionCount160{};
    std::uint8_t featureBranch{};
};

struct InstalledRow {
    std::uint16_t vendorIndex{};
    std::uint16_t rowIndex{};
    std::array<std::uint8_t, kInstalledRowStride> raw{};
};

} // namespace dawn::state::build_data::vendors
