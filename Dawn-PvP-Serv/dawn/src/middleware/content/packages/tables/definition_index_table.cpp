#include "definition_index_table.h"

#include <cstring>
#include <limits>

namespace dawn::middleware::content::packages::tables {
namespace {

// globals container names its children after an 8-byte prefix and its first field
constexpr std::size_t kChildTableOffset = 16;
constexpr std::size_t kSlotTableOffset = 8;
constexpr std::size_t kChildStride = 16;
constexpr std::size_t kContainerPrefix = 8;
constexpr std::size_t kDescriptorStride = 8;
// array header is count, element class, then data
constexpr std::size_t kHeaderCountOffset = 0;
constexpr std::size_t kHeaderClassOffset = 8;
constexpr std::size_t kHeaderDataOffset = 16;
constexpr std::size_t kHeaderMarkerBack = 4;
constexpr std::uint32_t kTagHighHalf = 0x8080;
constexpr std::size_t kIndexRowStride = 24;
// target tag follows the hash and two opaque fields
constexpr std::size_t kIndexRowTagOffset = 16;
// arrays larger than this are not definition tables
constexpr std::uint64_t kMaximumArrayCount = 300000;

template <typename Value>
[[nodiscard]] bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    if (offset > blob.size() || blob.size() - offset < sizeof value) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

[[nodiscard]] bool valid_header(std::span<const std::byte> blob,
                                std::size_t headerOffset,
                                std::uint64_t count,
                                std::uint32_t& elementClass) noexcept {
    std::uint64_t headerCount = 0;
    std::uint32_t marker = 0;
    if (headerOffset < kHeaderMarkerBack
        || !read(blob, headerOffset + kHeaderCountOffset, headerCount) || headerCount != count
        || !read(blob, headerOffset - kHeaderMarkerBack, marker)
        || !read(blob, headerOffset + kHeaderClassOffset, elementClass)) {
        return false;
    }
    return (marker >> 16U) == kTagHighHalf && (elementClass >> 16U) == kTagHighHalf;
}

[[nodiscard]] bool
resolve_descriptor(std::span<const std::byte> blob, std::size_t offset, Array& output) noexcept {
    output = {};
    std::uint64_t count = 0;
    std::int64_t relative = 0;
    constexpr std::int64_t kMaximumOffset = (std::numeric_limits<std::int64_t>::max)();
    constexpr std::int64_t kMinimumOffset = (std::numeric_limits<std::int64_t>::min)();
    if (offset > static_cast<std::size_t>(kMaximumOffset) - kDescriptorStride) {
        return false;
    }
    const std::int64_t base = static_cast<std::int64_t>(offset + kDescriptorStride);
    if (!read(blob, offset, count) || count == 0 || count > kMaximumArrayCount
        || !read(blob, offset + kDescriptorStride, relative)) {
        return false;
    }
    // offset is signed; an array header may sit before the descriptor that names it
    if ((relative > 0 && base > kMaximumOffset - relative)
        || (relative < 0 && base < kMinimumOffset - relative)) {
        return false;
    }
    const std::int64_t header = base + relative;
    if (header < static_cast<std::int64_t>(kHeaderMarkerBack)
        || static_cast<std::uint64_t>(header) + kHeaderDataOffset > blob.size()) {
        return false;
    }
    const auto headerOffset = static_cast<std::size_t>(header);
    std::uint32_t elementClass = 0;
    if (!valid_header(blob, headerOffset, count, elementClass)) {
        return false;
    }
    output = Array{count, headerOffset + kHeaderDataOffset, elementClass};
    return true;
}

} // namespace

bool find_array_at(std::span<const std::byte> blob,
                   std::size_t descriptorOffset,
                   Array& output) noexcept {
    return resolve_descriptor(blob, descriptorOffset, output);
}

bool find_array(std::span<const std::byte> blob,
                std::uint32_t elementClass,
                Array& output) noexcept {
    output = {};
    if (blob.size() <= kDescriptorStride * 2) {
        return false;
    }
    for (std::size_t offset = 0; offset + (kDescriptorStride * 2) <= blob.size();
         offset += kDescriptorStride) {
        Array candidate{};
        if (resolve_descriptor(blob, offset, candidate) && candidate.elementClass == elementClass) {
            output = candidate;
            return true;
        }
    }
    return false;
}

std::size_t child_count(std::span<const std::byte> blob) noexcept {
    return blob.size() <= kContainerPrefix ? 0 : (blob.size() - kContainerPrefix) / kChildStride;
}

bool child_tag(std::span<const std::byte> blob, std::size_t index, std::uint32_t& tag) noexcept {
    tag = 0;
    return index < child_count(blob) && read(blob, kChildTableOffset + index * kChildStride, tag);
}

bool slot_tag(std::span<const std::byte> blob, std::size_t index, std::uint32_t& tag) noexcept {
    tag = 0;
    return read(blob, kSlotTableOffset + index * kChildStride, tag);
}

bool index_row(std::span<const std::byte> blob,
               const Array& array,
               std::uint64_t index,
               IndexRow& row) noexcept {
    row = {};
    if (index >= array.count) {
        return false;
    }
    const std::size_t offset = array.dataOffset + static_cast<std::size_t>(index * kIndexRowStride);
    return read(blob, offset, row.definitionHash)
           && read(blob, offset + kIndexRowTagOffset, row.targetTag);
}

bool visit_index_rows(std::span<const std::byte> blob,
                      const Array& array,
                      RowVisitor visitor,
                      void* context) noexcept {
    if (visitor == nullptr || array.count == 0) {
        return false;
    }
    const std::uint64_t tableSize = kIndexRowStride * array.count;
    if (tableSize > blob.size() || array.dataOffset > blob.size() - tableSize) {
        return false;
    }
    for (std::uint64_t index = 0; index < array.count; ++index) {
        const std::size_t offset =
            array.dataOffset + static_cast<std::size_t>(index * kIndexRowStride);
        IndexRow row{};
        if (!read(blob, offset, row.definitionHash)
            || !read(blob, offset + kIndexRowTagOffset, row.targetTag)) {
            return false;
        }
        if (!visitor(context, static_cast<std::uint32_t>(index), row)) {
            return false;
        }
    }
    return true;
}

} // namespace dawn::middleware::content::packages::tables
