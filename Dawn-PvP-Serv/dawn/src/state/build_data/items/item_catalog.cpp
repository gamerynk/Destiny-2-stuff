#include "item_catalog.h"

#include <algorithm>
#include <array>
#include <limits>

#include "../table.h"

namespace dawn::state::build_data::items {
namespace {

constexpr std::size_t kLookupCapacity = kDefinitionCapacity * 2;
constexpr std::uint16_t kEmptyLookupRow = (std::numeric_limits<std::uint16_t>::max)();
constexpr std::uint64_t kHashOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kHashPrime = 1099511628211ULL;
constexpr std::size_t kDefinitionHashByteCount = sizeof(std::uint32_t);

Lock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;
std::array<std::uint16_t, kLookupCapacity> g_lookup{};
std::array<std::uint16_t, kLookupCapacity> g_hashLookup{};

static_assert((kLookupCapacity & (kLookupCapacity - 1)) == 0);

[[nodiscard]] std::uint64_t mix_definition_hash(std::uint32_t definitionHash) noexcept {
    std::uint64_t hash = kHashOffsetBasis;
    std::uint32_t value = definitionHash;
    for (std::size_t index = 0; index < kDefinitionHashByteCount; ++index) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= kHashPrime;
        value >>= 8U;
    }
    return hash;
}

[[nodiscard]] std::size_t start_slot(const Definition& definition) noexcept {
    std::uint64_t hash = mix_definition_hash(definition.definitionHash);
    hash ^= definition.bucketId;
    hash *= kHashPrime;
    return static_cast<std::size_t>(hash) & (kLookupCapacity - 1);
}

[[nodiscard]] std::size_t start_hash_slot(std::uint32_t definitionHash) noexcept {
    return static_cast<std::size_t>(mix_definition_hash(definitionHash)) & (kLookupCapacity - 1);
}

void insert_lookup(const Definition& definition) noexcept {
    const std::size_t start = start_slot(definition);
    for (std::size_t probe = 0; probe < g_lookup.size(); ++probe) {
        std::uint16_t& row = g_lookup[(start + probe) & (g_lookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            row = definition.definitionIndex;
            return;
        }
    }
}

void insert_hash_lookup(const Definition& definition) noexcept {
    const std::size_t start = start_hash_slot(definition.definitionHash);
    for (std::size_t probe = 0; probe < g_hashLookup.size(); ++probe) {
        std::uint16_t& row = g_hashLookup[(start + probe) & (g_hashLookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            row = definition.definitionIndex;
            return;
        }
    }
}

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_definitions.clear();
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
    std::fill(g_hashLookup.begin(), g_hashLookup.end(), kEmptyLookupRow);
}

bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }
    std::array<bool, kDefinitionCapacity> occupied{};
    for (const Definition& definition : definitions) {
        if (definition.definitionIndex >= definitions.size()
            || occupied[definition.definitionIndex]) {
            return false;
        }
        occupied[definition.definitionIndex] = true;
    }
    return true;
}

bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    const Lock::Exclusive guard(g_lock);
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
    std::fill(g_hashLookup.begin(), g_hashLookup.end(), kEmptyLookupRow);
    // valid() proved each index appears once, so every row lands in its own slot
    const std::span<Definition> storage = g_definitions.reset(definitions.size());
    if (storage.size() != definitions.size()) {
        return false;
    }
    for (const Definition& definition : definitions) {
        storage[definition.definitionIndex] = definition;
        insert_hash_lookup(definition);
        // a row with no bucket is still reachable by native index, but not by bucket lookup
        if (definition.bucketId != kUnresolvedBucketId) {
            insert_lookup(definition);
        }
    }
    return true;
}

bool find_hash(std::uint32_t definitionHash, Definition& definition) noexcept {
    definition = {};
    const std::size_t start = start_hash_slot(definitionHash);
    std::uint16_t match = kEmptyLookupRow;
    bool ambiguous = false;
    const Lock::Shared guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    for (std::size_t probe = 0; probe < g_hashLookup.size(); ++probe) {
        const std::uint16_t row = g_hashLookup[(start + probe) & (g_hashLookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            break;
        }
        if (rows[row].definitionHash == definitionHash) {
            ambiguous = match != kEmptyLookupRow;
            match = row;
        }
    }
    if (!ambiguous && match != kEmptyLookupRow) {
        definition = rows[match];
    }
    return !ambiguous && match != kEmptyLookupRow;
}

bool find(std::uint32_t definitionHash, std::uint8_t bucketId, Definition& definition) noexcept {
    definition = {};
    if (bucketId == kUnresolvedBucketId) {
        return false;
    }
    const Definition key{definitionHash, 0, bucketId};
    const std::size_t start = start_slot(key);
    std::uint16_t match = kEmptyLookupRow;
    bool ambiguous = false;
    const Lock::Shared guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    for (std::size_t probe = 0; probe < g_lookup.size(); ++probe) {
        const std::uint16_t row = g_lookup[(start + probe) & (g_lookup.size() - 1)];
        if (row == kEmptyLookupRow) {
            break;
        }
        const Definition& candidate = rows[row];
        if (candidate.definitionHash == definitionHash && candidate.bucketId == bucketId) {
            ambiguous = match != kEmptyLookupRow;
            match = row;
        }
    }
    if (!ambiguous && match != kEmptyLookupRow) {
        definition = rows[match];
    }
    return !ambiguous && match != kEmptyLookupRow;
}

bool find_index(std::uint16_t definitionIndex, Definition& definition) noexcept {
    definition = {};
    const Lock::Shared guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    const bool found = static_cast<std::size_t>(definitionIndex) < rows.size();
    if (found) {
        definition = rows[definitionIndex];
    }
    return found;
}

bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.snapshot(output, count);
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.count();
}

} // namespace dawn::state::build_data::items
