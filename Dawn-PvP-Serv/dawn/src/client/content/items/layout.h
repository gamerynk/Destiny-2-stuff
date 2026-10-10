#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::client::content::items::layout {

inline constexpr std::size_t kTableRowCountOffset = 8;
inline constexpr std::size_t kTableFirstRowOffset = 48;
inline constexpr std::size_t kRowTargetHandleOffset = 16;
inline constexpr std::size_t kItemIndexRowSize = 24;
inline constexpr std::size_t kDefinitionBucketOffset = 184;

struct ItemIndexRow {
    std::uint32_t definitionHash{};
    std::array<std::byte, kRowTargetHandleOffset - sizeof(std::uint32_t)> opaque04{};
    std::uint32_t targetHandle{};
    std::array<std::byte, kItemIndexRowSize - kRowTargetHandleOffset - sizeof(std::uint32_t)>
        opaque20{};
};

struct ItemTable {
    std::array<std::byte, kTableRowCountOffset> opaque00{};
    std::uint64_t rowCount{};
    std::array<std::byte, kTableFirstRowOffset - kTableRowCountOffset - sizeof(std::uint64_t)>
        opaque16{};
    ItemIndexRow firstRow{};
};

struct ItemDefinition {
    std::array<std::byte, kDefinitionBucketOffset> opaque00{};
    std::uint8_t bucketId{};
};

static_assert(offsetof(ItemIndexRow, targetHandle) == kRowTargetHandleOffset);
static_assert(sizeof(ItemIndexRow) == kItemIndexRowSize);
static_assert(offsetof(ItemTable, rowCount) == kTableRowCountOffset);
static_assert(offsetof(ItemTable, firstRow) == kTableFirstRowOffset);
static_assert(offsetof(ItemDefinition, bucketId) == kDefinitionBucketOffset);

} // namespace dawn::client::content::items::layout
