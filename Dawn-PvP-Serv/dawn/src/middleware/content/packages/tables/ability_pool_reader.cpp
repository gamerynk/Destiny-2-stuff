#include "ability_pool_reader.h"

#include <cstring>

#include "definition_index_table.h"

namespace dawn::middleware::content::packages::tables::abilities {
namespace {

constexpr std::size_t kEntryDescriptor = 16;
constexpr std::size_t kEntryStride = 64;
constexpr std::size_t kEntryPlugSource = 8;
constexpr std::size_t kEntryGroup = 12;
constexpr std::size_t kEntryKind = 13;
constexpr std::size_t kEntrySelector = 32;
constexpr std::size_t kEntryPool = 56;

constexpr std::size_t kPoolGroupCount = 8;
constexpr std::size_t kPoolGroupArray = 16;
constexpr std::size_t kPoolGroupStride = 144;
constexpr std::size_t kPoolGroupHeader = 16;
constexpr std::size_t kGroupVariantOffset = 8;
constexpr std::size_t kGroupVariantArray = 32;
constexpr std::size_t kVariantStride = 88;
constexpr std::size_t kVariantCountBack = 8;
constexpr std::size_t kVariantRecordHeader = 16;
constexpr std::size_t kPoolRecordStride = 16;
// a pool with one group and a live entry selector picks its variant by this state, not the last
constexpr std::int32_t kActiveStateSelector = 18;
constexpr std::int64_t kSingleGroupPool = 1;

constexpr std::size_t kRecordHash = 0;
constexpr std::size_t kRecordCategory = 4;
constexpr std::size_t kRecordLinkEntry = 8;
constexpr std::size_t kRecordLinkSubgroup = 9;
constexpr std::size_t kRecordLinkElement = 10;
constexpr std::size_t kRecordKind = 11;
constexpr std::size_t kRecordDestination = 12;

template <typename Value>
[[nodiscard]] bool
read(std::span<const std::byte> blob, std::size_t offset, Value& value) noexcept {
    if (offset > blob.size() || blob.size() - offset < sizeof value) {
        return false;
    }
    std::memcpy(&value, blob.data() + offset, sizeof value);
    return true;
}

[[nodiscard]] bool
follow(std::span<const std::byte> blob, std::size_t member, std::size_t& resolved) noexcept {
    std::int64_t relative = 0;
    if (!read(blob, member, relative)) {
        return false;
    }
    const std::int64_t target = static_cast<std::int64_t>(member) + relative;
    if (target < 0 || static_cast<std::uint64_t>(target) >= blob.size()) {
        return false;
    }
    resolved = static_cast<std::size_t>(target);
    return true;
}

[[nodiscard]] std::int32_t
active_variant(std::int64_t groupCount, std::int32_t variantCount, const Entry& entry) noexcept {
    if (groupCount == kSingleGroupPool && entry.hasSelector && entry.selector != kEmptyByte) {
        return kActiveStateSelector % variantCount;
    }
    return variantCount - 1;
}

} // namespace

std::size_t read_entries(std::span<const std::byte> blob, std::span<Entry> output) noexcept {
    Array entries{};
    if (!find_array_at(blob, kEntryDescriptor, entries)) {
        return 0;
    }
    std::size_t count = 0;
    while (count < entries.count && count < output.size()) {
        const std::size_t base = entries.dataOffset + count * kEntryStride;
        Entry entry{};
        if (!read(blob, base + kEntryPlugSource, entry.plugSource)
            || !read(blob, base + kEntryGroup, entry.group)
            || !read(blob, base + kEntryKind, entry.kind)
            || !read(blob, base + kEntryPool, entry.poolTag)) {
            break;
        }
        // selector is self-relative; zero when the entry has none
        std::int64_t selectorRelative = 0;
        std::size_t selectorOffset = 0;
        if (read(blob, base + kEntrySelector, selectorRelative) && selectorRelative != 0
            && follow(blob, base + kEntrySelector, selectorOffset)) {
            entry.hasSelector = read(blob, selectorOffset, entry.selector);
        }
        output[count++] = entry;
    }
    return count;
}

std::size_t read_pool_records(std::span<const std::byte> blob,
                              const Entry& entry,
                              std::uint8_t subgroup,
                              std::span<PoolRecord> output) noexcept {
    std::int64_t groupCount = 0;
    std::size_t groupArray = 0;
    if (!read(blob, kPoolGroupCount, groupCount) || !follow(blob, kPoolGroupArray, groupArray)) {
        return 0;
    }
    const std::size_t group =
        groupArray + kPoolGroupHeader + static_cast<std::size_t>(subgroup) * kPoolGroupStride;
    std::int32_t variantCount = 0;
    std::size_t variantArray = 0;
    if (!read(blob, group, variantCount) || variantCount <= 0
        || !follow(blob, group + kGroupVariantOffset, variantArray)) {
        return 0;
    }
    const auto variant = static_cast<std::size_t>(active_variant(groupCount, variantCount, entry));
    const std::size_t variantField =
        variantArray + kGroupVariantArray - kGroupVariantOffset + variant * kVariantStride;
    std::int32_t recordCount = 0;
    std::size_t records = 0;
    if (variantField < kVariantCountBack
        || !read(blob, variantField - kVariantCountBack, recordCount) || recordCount <= 0
        || !follow(blob, variantField, records)) {
        return 0;
    }
    records += kVariantRecordHeader;
    std::size_t count = 0;
    while (count < static_cast<std::size_t>(recordCount) && count < output.size()) {
        const std::size_t base = records + count * kPoolRecordStride;
        PoolRecord record{};
        if (!read(blob, base + kRecordHash, record.definitionHash)
            || !read(blob, base + kRecordCategory, record.category)
            || !read(blob, base + kRecordLinkEntry, record.linkEntry)
            || !read(blob, base + kRecordLinkSubgroup, record.linkSubgroup)
            || !read(blob, base + kRecordLinkElement, record.linkElement)
            || !read(blob, base + kRecordKind, record.kind)
            || !read(blob, base + kRecordDestination, record.destination)) {
            break;
        }
        output[count++] = record;
    }
    return count;
}

} // namespace dawn::middleware::content::packages::tables::abilities
