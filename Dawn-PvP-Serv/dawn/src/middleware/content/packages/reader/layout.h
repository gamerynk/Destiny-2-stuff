#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dawn::middleware::content::packages::reader::layout {

inline constexpr std::uint16_t kSupportedVersion = 38;
inline constexpr std::size_t kHeaderSize = 0x180;
inline constexpr std::size_t kBlockSize = 0x40000;
inline constexpr std::uint32_t kEntryTableAdjustment = 96;
inline constexpr std::size_t kBlockTableGap = 32;
inline constexpr std::uint32_t kTagBase = 0x80800000;
inline constexpr std::uint32_t kTagEntryBits = 13;
inline constexpr std::uint32_t kTagEntryMask = (1U << kTagEntryBits) - 1U;

struct HeaderOffsets {
    static constexpr std::size_t kVersion = 0x00;
    static constexpr std::size_t kPackageId = 0x04;
    static constexpr std::size_t kPatchId = 0x20;
    static constexpr std::size_t kEntryCount = 0xB4;
    static constexpr std::size_t kBlockCount = 0xD0;
    static constexpr std::size_t kEntryTable = 0x110;
};

struct EntryRecord {
    std::uint32_t reference{};
    std::uint32_t typeInfo{};
    std::uint64_t blockInfo{};
};

struct BlockRecord {
    std::uint32_t offset{};
    std::uint32_t size{};
    std::uint16_t patchId{};
    std::uint16_t flags{};
    std::array<std::byte, 20> opaque{};
    std::array<std::byte, 16> tag{};
};

struct BlockFlags {
    static constexpr std::uint16_t kCompressed = 0x1;
    static constexpr std::uint16_t kEncrypted = 0x2;
    static constexpr std::uint16_t kAlternateKey = 0x4;
};

struct EntryPlacement {
    std::uint32_t startBlock{};
    std::uint32_t startOffset{};
    std::uint32_t size{};
};

inline constexpr std::size_t kEntryRecordSize = 16;
inline constexpr std::size_t kBlockRecordSize = 48;

static_assert(sizeof(EntryRecord) == kEntryRecordSize);
static_assert(sizeof(BlockRecord) == kBlockRecordSize);
static_assert(std::is_trivially_copyable_v<EntryRecord>);
static_assert(std::is_trivially_copyable_v<BlockRecord>);

[[nodiscard]] constexpr EntryPlacement placement(const EntryRecord& record) noexcept {
    return EntryPlacement{
        static_cast<std::uint32_t>(record.blockInfo & 0x3FFF),
        static_cast<std::uint32_t>(((record.blockInfo >> 14) & 0x3FFF) << 4),
        static_cast<std::uint32_t>(record.blockInfo >> 28),
    };
}

} // namespace dawn::middleware::content::packages::reader::layout
