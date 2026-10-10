#include "codec.h"

namespace dawn::state::build_data::cache::records {
namespace {

constexpr unsigned int kReservedFieldValue = 0;

} // namespace

bool encode(const abilities::Definition& value, AbilityBucketRecord& record) noexcept {
    record = {};
    record.socketEntryListIndex = value.socketEntryListIndex;
    record.movementEntry = value.selection.movementEntry;
    record.grenadeEntry = value.selection.grenadeEntry;
    record.superEntry = value.selection.superEntry;
    record.meleeEntry = value.selection.meleeEntry;
    record.classEntry = value.selection.classEntry;
    record.overflowCount = value.overflowCount;
    record.overflow = value.overflow;
    for (std::size_t bucket = 0; bucket < abilities::kBucketCapacity; ++bucket) {
        record.bucketKinds[bucket] = value.buckets[bucket].kind;
        record.bucketHashCounts[bucket] = value.buckets[bucket].hashCount;
        for (std::size_t entry = 0; entry < abilities::kBucketHashCapacity; ++entry) {
            record.bucketHashes[bucket * abilities::kBucketHashCapacity + entry] =
                value.buckets[bucket].hashes[entry];
        }
    }
    return true;
}

bool decode(const AbilityBucketRecord& record, abilities::Definition& value) noexcept {
    value = {};
    if (record.overflowCount > abilities::kOverflowCapacity) {
        return false;
    }
    value.socketEntryListIndex = record.socketEntryListIndex;
    value.selection.movementEntry = record.movementEntry;
    value.selection.grenadeEntry = record.grenadeEntry;
    value.selection.superEntry = record.superEntry;
    value.selection.meleeEntry = record.meleeEntry;
    value.selection.classEntry = record.classEntry;
    value.overflowCount = record.overflowCount;
    value.overflow = record.overflow;
    for (std::size_t bucket = 0; bucket < abilities::kBucketCapacity; ++bucket) {
        if (record.bucketHashCounts[bucket] > abilities::kBucketHashCapacity) {
            return false;
        }
        value.buckets[bucket].kind = record.bucketKinds[bucket];
        value.buckets[bucket].hashCount = record.bucketHashCounts[bucket];
        for (std::size_t entry = 0; entry < abilities::kBucketHashCapacity; ++entry) {
            value.buckets[bucket].hashes[entry] =
                record.bucketHashes[bucket * abilities::kBucketHashCapacity + entry];
        }
    }
    return true;
}

bool encode(const progressions::Definition& value, ProgressionRecord& record) noexcept {
    record = {
        value.definitionIndex,
        static_cast<std::uint8_t>(value.scope),
        kReservedFieldValue,
    };
    return true;
}

bool decode(const ProgressionRecord& record, progressions::Definition& value) noexcept {
    value = {};
    if (record.reserved != kReservedFieldValue) {
        return false;
    }
    value = {record.definitionIndex, static_cast<progressions::Scope>(record.scope)};
    return true;
}

} // namespace dawn::state::build_data::cache::records
