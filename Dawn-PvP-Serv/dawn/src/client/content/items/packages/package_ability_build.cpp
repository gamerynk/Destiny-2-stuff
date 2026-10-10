#include <array>

#include "../../../../middleware/content/packages/tables/ability_pool_reader.h"
#include "../../../../state/account/account_state.h"
#include "../../../../state/build_data/runtime.h"
#include "../../../../state/runtime/runtime.h"
#include "internal.h"

namespace dawn::client::content::items::packages {
namespace {

namespace pool = middleware::content::packages::tables::abilities;
namespace domain = state::build_data::abilities;

// sprint ability socket entry, the one entry the character cannot choose
constexpr std::uint8_t kSprintEntry = 1;
constexpr std::size_t kSummaryEntryCount = 6;
// entry kind of the super, which stays active without a plug source of its own
constexpr std::uint8_t kSuperKind = 34;
constexpr std::size_t kSelectorChainLimit = 8;

struct Walk {
    const reader::Source* source{};
    reader::Scratch* scratch{};
    std::vector<std::byte>* blob{};
    std::array<pool::Entry, pool::kEntryCapacity> entries{};
    std::size_t entryCount{};
    std::array<std::uint8_t, kSummaryEntryCount> selected{};
};

[[nodiscard]] std::array<std::uint8_t, kSummaryEntryCount>
summary_entries(const domain::Selection& selection) noexcept {
    // matches family4::loadout::subclass_selection: a bottom-tree melee and super can share a
    // group, and the melee must claim the plug source first or that tree is missing from the bank
    return {selection.grenadeEntry,
            selection.meleeEntry,
            selection.superEntry,
            selection.movementEntry,
            kSprintEntry,
            selection.classEntry};
}

[[nodiscard]] std::size_t records_of(const Walk& walk,
                                     const pool::Entry& entry,
                                     std::uint8_t subgroup,
                                     std::span<pool::PoolRecord> records) noexcept {
    if (entry.poolTag == 0
        || !reader::read_tag(*walk.source, *walk.scratch, entry.poolTag, *walk.blob)) {
        return 0;
    }
    return pool::read_pool_records(
        std::span<const std::byte>{*walk.blob}, entry, subgroup, records);
}

[[nodiscard]] bool
selector_destination(const Walk& walk, std::uint8_t entryIndex, std::uint8_t& bucket) noexcept {
    std::uint8_t subgroup = 0;
    std::uint8_t element = 0;
    for (std::size_t step = 0; step < kSelectorChainLimit; ++step) {
        std::array<pool::PoolRecord, pool::kPoolRecordCapacity> records{};
        const std::size_t count =
            entryIndex < walk.entryCount
                ? records_of(walk, walk.entries[entryIndex], subgroup, records)
                : 0;
        if (element >= count) {
            return false;
        }
        const pool::PoolRecord& record = records[element];
        if (record.destination != pool::kEmptyByte) {
            bucket = record.destination;
            return bucket < domain::kBucketCapacity;
        }
        if (record.linkEntry == pool::kEmptyByte || record.linkSubgroup == pool::kEmptyByte
            || record.linkElement == pool::kEmptyByte) {
            return false;
        }
        entryIndex = record.linkEntry;
        subgroup = record.linkSubgroup;
        element = record.linkElement;
    }
    return false;
}

void chosen_sources(const Walk& walk, std::array<std::uint32_t, 256>& sources) noexcept {
    sources.fill(pool::kNoPlugSource);
    for (const std::uint8_t entryIndex : walk.selected) {
        if (entryIndex >= walk.entryCount) {
            continue;
        }
        const pool::Entry& entry = walk.entries[entryIndex];
        if (entry.plugSource != pool::kNoPlugSource
            && sources[entry.group] == pool::kNoPlugSource) {
            sources[entry.group] = entry.plugSource;
        }
    }
}

[[nodiscard]] bool active(const pool::Entry& entry,
                          const std::array<std::uint32_t, 256>& sources) noexcept {
    if (entry.plugSource == pool::kNoPlugSource) {
        return entry.kind == kSuperKind;
    }
    return sources[entry.group] == entry.plugSource;
}

[[nodiscard]] bool assign_kinds(const Walk& walk, domain::Definition& output) noexcept {
    for (const std::uint8_t entryIndex : walk.selected) {
        if (entryIndex >= walk.entryCount) {
            return false;
        }
        std::array<pool::PoolRecord, pool::kPoolRecordCapacity> records{};
        std::uint8_t bucket = 0;
        if (records_of(walk, walk.entries[entryIndex], 0, records) == 0
            || records[0].kind == pool::kEmptyByte
            || !selector_destination(walk, entryIndex, bucket)
            || output.buckets[bucket].kind != domain::kEmptyBucketKind) {
            return false;
        }
        output.buckets[bucket].kind = records[0].kind;
    }
    return true;
}

void file_hash(const pool::PoolRecord& record, domain::Definition& output) noexcept {
    if (record.definitionHash == pool::kNoPlugSource) {
        return;
    }
    if (record.category == pool::kEmptyByte) {
        if (output.overflowCount < output.overflow.size()) {
            output.overflow[output.overflowCount++] = record.definitionHash;
        }
        return;
    }
    for (domain::Bucket& bucket : output.buckets) {
        if (bucket.kind == record.category && bucket.hashCount < bucket.hashes.size()) {
            bucket.hashes[bucket.hashCount++] = record.definitionHash;
        }
    }
}

} // namespace

bool build_ability_buckets(const reader::Source& source,
                           reader::Scratch& scratch,
                           std::span<const std::byte> listDefinition,
                           std::vector<std::byte>& blob,
                           const state::build_data::abilities::Selection& selection,
                           state::build_data::abilities::Definition& output) noexcept {
    Walk walk{};
    walk.source = &source;
    walk.scratch = &scratch;
    walk.blob = &blob;
    walk.entryCount = pool::read_entries(listDefinition, walk.entries);
    if (walk.entryCount == 0) {
        return false;
    }
    walk.selected = summary_entries(selection);

    for (domain::Bucket& bucket : output.buckets) {
        bucket = {};
    }
    output.overflowCount = 0;
    if (!assign_kinds(walk, output)) {
        return false;
    }
    std::array<std::uint32_t, 256> sources{};
    chosen_sources(walk, sources);
    for (std::size_t entryIndex = 0; entryIndex < walk.entryCount; ++entryIndex) {
        if (!active(walk.entries[entryIndex], sources)) {
            continue;
        }
        std::array<pool::PoolRecord, pool::kPoolRecordCapacity> records{};
        const std::size_t count = records_of(walk, walk.entries[entryIndex], 0, records);
        for (std::size_t entry = 0; entry < count; ++entry) {
            file_hash(records[entry], output);
        }
    }
    return true;
}

} // namespace dawn::client::content::items::packages
