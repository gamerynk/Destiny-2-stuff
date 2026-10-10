#include <cstring>

#include "internal.h"

namespace dawn::middleware::content::packages::reader {
namespace {

template <typename Value>
[[nodiscard]] Value field(std::span<const std::byte, layout::kHeaderSize> bytes,
                          std::size_t offset) noexcept {
    Value value{};
    std::memcpy(&value, bytes.data() + offset, sizeof value);
    return value;
}

} // namespace

bool parse_header(std::span<const std::byte, layout::kHeaderSize> bytes, Header& header) noexcept {
    header = {};
    header.version = field<std::uint16_t>(bytes, layout::HeaderOffsets::kVersion);
    if (header.version != layout::kSupportedVersion) {
        return false;
    }
    header.packageId = field<std::uint16_t>(bytes, layout::HeaderOffsets::kPackageId);
    header.patchId = field<std::uint16_t>(bytes, layout::HeaderOffsets::kPatchId);
    header.entryCount = field<std::uint32_t>(bytes, layout::HeaderOffsets::kEntryCount);
    header.blockCount = field<std::uint32_t>(bytes, layout::HeaderOffsets::kBlockCount);
    header.entryTable =
        static_cast<std::uint64_t>(field<std::uint32_t>(bytes, layout::HeaderOffsets::kEntryTable))
        + layout::kEntryTableAdjustment;
    header.blockTable =
        header.entryTable
        + static_cast<std::uint64_t>(header.entryCount) * sizeof(layout::EntryRecord)
        + layout::kBlockTableGap;
    return header.entryCount != 0 && header.blockCount != 0;
}

} // namespace dawn::middleware::content::packages::reader
