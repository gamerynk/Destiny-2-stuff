#include <algorithm>
#include <cstring>
#include <limits>

#include "internal.h"

namespace dawn::middleware::content::packages::named_tags {
namespace {

[[nodiscard]] bool
add_fits(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept {
    if (right > (std::numeric_limits<std::uint64_t>::max)() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool
multiply_fits(std::uint64_t left, std::uint64_t right, std::uint64_t& result) noexcept {
    if (left != 0 && right > (std::numeric_limits<std::uint64_t>::max)() / left) {
        return false;
    }
    result = left * right;
    return true;
}

template <typename Value>
[[nodiscard]] bool read_object(const Reader& reader, std::uint64_t offset, Value& value) noexcept {
    std::uint64_t end = 0;
    if (reader.read == nullptr || !add_fits(offset, sizeof value, end) || end > reader.size) {
        return false;
    }
    return reader.read(
        reader.context, offset, std::span(reinterpret_cast<std::byte*>(&value), sizeof value));
}

[[nodiscard]] bool read_directory(const Reader& reader,
                                  const PackageHeader& header,
                                  std::uint64_t& offset,
                                  RelativeDirectory& directory) noexcept {
    if (header.variant == Variant::beta) {
        offset =
            static_cast<std::uint64_t>(header.miscOffset) + offsetof(BetaMiscDirectory, namedTags);
    } else if (header.variant == Variant::preBeyondLight) {
        offset = static_cast<std::uint64_t>(header.miscOffset)
                 + offsetof(PreBeyondLightMiscDirectory, namedTags);
    } else {
        return false;
    }
    return read_object(reader, offset, directory);
}

[[nodiscard]] bool read_name(const Reader& reader, std::uint64_t offset, Entry& entry) noexcept {
    if (offset >= reader.size) {
        return false;
    }
    const std::size_t available = static_cast<std::size_t>(
        (std::min)(reader.size - offset, static_cast<std::uint64_t>(entry.name.size())));
    std::array<std::byte, kNameCapacity> bytes{};
    if (!reader.read(reader.context, offset, std::span(bytes).first(available))) {
        return false;
    }
    const auto availableBytes = std::span(bytes).first(available);
    const auto terminator = std::find(availableBytes.begin(), availableBytes.end(), std::byte{});
    if (terminator == availableBytes.begin() || terminator == availableBytes.end()) {
        return false;
    }
    entry.nameLength = static_cast<std::size_t>(terminator - availableBytes.begin());
    std::memcpy(entry.name.data(), bytes.data(), entry.nameLength);
    return true;
}

} // namespace

static_assert(sizeof(PackageHeader) == kPackageHeaderSize);
static_assert(sizeof(RelativeDirectory) == kRelativeDirectorySize);
static_assert(sizeof(Record) == kRecordSize);

bool extract(const Reader& reader, Visitor visitor, void* context, Result& result) noexcept {
    result = {};
    PackageHeader header{};
    if (visitor == nullptr || !read_object(reader, 0, header)
        || header.version != kSupportedVersion) {
        return false;
    }
    // a package with no misc directory has no named definitions, which is still valid
    if (header.miscOffset == 0) {
        return true;
    }

    std::uint64_t directoryOffset = 0;
    RelativeDirectory directory{};
    if (!read_directory(reader, header, directoryOffset, directory)
        || directory.count > kEntryCountLimit) {
        return false;
    }
    std::uint64_t tableOffset = 0;
    std::uint64_t tableBytes = 0;
    if (!add_fits(directoryOffset, sizeof directory, tableOffset)
        || !add_fits(tableOffset, directory.relativeOffset, tableOffset)
        || !multiply_fits(directory.count, sizeof(Record), tableBytes) || tableOffset > reader.size
        || tableBytes > reader.size - tableOffset) {
        return false;
    }

    for (std::uint64_t index = 0; index < directory.count; ++index) {
        const std::uint64_t recordOffset = tableOffset + index * sizeof(Record);
        Record record{};
        Entry entry{};
        std::uint64_t nameAnchor = 0;
        std::uint64_t nameOffset = 0;
        if (!read_object(reader, recordOffset, record)
            || !add_fits(recordOffset, offsetof(Record, nameRelativeOffset), nameAnchor)
            || !add_fits(nameAnchor, record.nameRelativeOffset, nameOffset)
            || !read_name(reader, nameOffset, entry)) {
            return false;
        }
        entry.tag = record.tag;
        entry.classId = record.classId;
        if (!visitor(context, entry)) {
            return false;
        }
        ++result.entries;
    }
    return true;
}

} // namespace dawn::middleware::content::packages::named_tags
