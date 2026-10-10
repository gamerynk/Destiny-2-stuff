#include <Windows.h>

#include <algorithm>
#include <string_view>

#include "../../../middleware/content/packages/named_tags.h"
#include "../../../middleware/content/packages/tables/bubble_state_reader.h"
#include "../../../middleware/content/packages/tables/scenario_reader.h"
#include "../spawn_sets/spawn_set_catalog_builder.h"
#include "internal.h"

namespace dawn::client::content::scenarios {
namespace {

namespace packages = middleware::content::packages;

constexpr std::string_view kNameSuffix = ":scenario_client";

// the stem keys the destination's spawn-set lookup, so both storages must agree or names truncate
static_assert(layouts::kSpawnStemCapacity == state::build_data::spawn_sets::kStemNameCapacity);

bool collect_tag(void* context, const reader::ClassEntry& entry) noexcept {
    auto& storage = *static_cast<Storage*>(context);
    if (storage.liveTagCount >= storage.liveTags.size()) {
        return false;
    }
    LiveTag& live = storage.liveTags[storage.liveTagCount++];
    live.tag = entry.tag;
    if (!spawn_sets::normalize_stem(entry.packageFamily, live.stem, live.stemLength)) {
        live.stem = {};
        live.stemLength = 0;
    }
    return true;
}

[[nodiscard]] const LiveTag* live_tag(const Storage& storage, std::uint32_t tag) noexcept {
    for (std::size_t index = 0; index < storage.liveTagCount; ++index) {
        if (storage.liveTags[index].tag == tag) {
            return &storage.liveTags[index];
        }
    }
    return nullptr;
}

// a destination is named once per patch with a different tag each; patch zero is visited
// first, so keeping the first one seen would keep a stale tag
bool collect_named(void* context, const packages::named_tags::Entry& entry) noexcept {
    auto& storage = *static_cast<Storage*>(context);
    const std::string_view name{entry.name.data(), entry.nameLength};
    if (entry.classId != packages::tables::kScenarioClass || !name.ends_with(kNameSuffix)) {
        return true;
    }
    const std::string_view destination = name.substr(0, name.size() - kNameSuffix.size());
    if (destination.empty() || destination.size() > layouts::kNameCapacity) {
        return true;
    }
    for (std::size_t row = 0; row < storage.rowCount; ++row) {
        layouts::Definition& existing = storage.rows[row];
        if (std::string_view(existing.name.data(), existing.nameLength) != destination) {
            continue;
        }
        if (entry.patchIndex >= storage.rowPatch[row]) {
            storage.rowPatch[row] = entry.patchIndex;
            existing.tag = entry.tag;
        }
        return true;
    }
    if (storage.rowCount >= storage.rows.size()) {
        return false;
    }
    layouts::Definition& definition = storage.rows[storage.rowCount];
    std::copy(destination.begin(), destination.end(), definition.name.begin());
    definition.nameLength = static_cast<std::uint8_t>(destination.size());
    definition.tag = entry.tag;
    storage.rowPatch[storage.rowCount] = entry.patchIndex;
    ++storage.rowCount;
    return true;
}

void add_package(layouts::Definition& definition, std::uint16_t packageId) noexcept {
    if (packageId == packages::tables::kAbsentPackageId
        || definition.packageCount >= definition.packages.size()) {
        return;
    }
    for (std::size_t index = 0; index < definition.packageCount; ++index) {
        if (definition.packages[index] == packageId) {
            return;
        }
    }
    definition.packages[definition.packageCount++] = packageId;
}

[[nodiscard]] bool resolve_row(const reader::Source& source,
                               reader::Scratch& scratch,
                               Storage& storage,
                               layouts::Definition& definition) noexcept {
    packages::tables::BubbleStates states{};
    if (!reader::read_tag(source, scratch, definition.tag, storage.blob)
        || !packages::tables::bubble_states(storage.blob, states)) {
        return false;
    }
    definition.bubbleCount = static_cast<std::uint8_t>(states.count);
    definition.truncated = states.truncated ? 1U : 0U;
    std::copy(states.bytes.begin(), states.bytes.end(), definition.bubbleStates.begin());
    std::copy(states.hashes.begin(), states.hashes.end(), definition.bubbleHashes.begin());
    std::copy(
        states.stateCounts.begin(), states.stateCounts.end(), definition.bubbleStateCounts.begin());
    std::copy(
        states.mapIndices.begin(), states.mapIndices.end(), definition.bubbleMapIndices.begin());
    definition.packageCount = 0;
    // the destination's own package loads too; otherwise a scenario whose bubbles name none claims nothing
    add_package(definition, packages::tables::package_of(definition.tag));
    for (std::size_t index = 0; index < states.packageCount; ++index) {
        add_package(definition, states.packages[index]);
    }
    return true;
}

} // namespace

bool collect_rows(const reader::Source& source, Storage& storage, const char*& reason) noexcept {
    reason = nullptr;
    storage.liveTagCount = 0;
    storage.rowCount = 0;
    storage.rowPatch.fill(0);
    packages::reader::ScanResult scan{};
    if (!packages::reader::scan_class_entries(
            source.directory, packages::tables::kScenarioClass, &collect_tag, &storage, scan)) {
        reason = "sweep";
        return false;
    }
    packages::named_tags::DirectoryResult names{};
    if (!packages::named_tags::extract_directory(
            source.directory, &collect_named, &storage, names)) {
        reason = "names";
        return false;
    }
    storage.resolved.fill(0);
    storage.resolvedCount = 0;
    storage.resolveCursor = 0;
    storage.liveRowCount = 0;
    for (std::size_t row = 0; row < storage.rowCount; ++row) {
        const LiveTag* const live = live_tag(storage, storage.rows[row].tag);
        if (live == nullptr) {
            continue;
        }
        ++storage.liveRowCount;
        storage.rows[row].spawnStem = live->stem;
        storage.rows[row].spawnStemLength = live->stemLength;
    }
    storage.resolveDeadlineTick = GetTickCount64() + kResolveWindowMs;
    return true;
}

bool resolve_pending(const reader::Source& source,
                     reader::Scratch& scratch,
                     Storage& storage) noexcept {
    std::size_t reads = 0;
    std::size_t visited = 0;
    while (visited < storage.rowCount && reads < kResolveReadBudget) {
        const std::size_t row = storage.resolveCursor;
        storage.resolveCursor = (storage.resolveCursor + 1) % storage.rowCount;
        ++visited;
        if (storage.resolved[row] != 0) {
            continue;
        }
        ++reads;
        if (resolve_row(source, scratch, storage, storage.rows[row])) {
            storage.resolved[row] = 1;
            ++storage.resolvedCount;
        }
    }
    return storage.resolvedCount >= storage.liveRowCount
           || GetTickCount64() >= storage.resolveDeadlineTick;
}

// keep rows and name match, clear only per-row read results: every read failed because the block keys were not ready
void rearm_resolve(Storage& storage) noexcept {
    storage.resolved.fill(0);
    storage.resolvedCount = 0;
    storage.resolveCursor = 0;
    storage.resolveDeadlineTick = GetTickCount64() + kResolveWindowMs;
    ++storage.resolveRounds;
}

void compact_rows(Storage& storage) noexcept {
    std::size_t kept = 0;
    for (std::size_t row = 0; row < storage.rowCount; ++row) {
        if (storage.resolved[row] != 0) {
            storage.rows[kept++] = storage.rows[row];
        }
    }
    storage.keptCount = kept;
    storage.blob.clear();
    storage.blob.shrink_to_fit();
}

} // namespace dawn::client::content::scenarios
