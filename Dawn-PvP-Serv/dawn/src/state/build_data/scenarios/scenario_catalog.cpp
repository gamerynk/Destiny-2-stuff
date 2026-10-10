#include "scenario_catalog.h"

#include "../table.h"

namespace dawn::state::build_data::scenarios {
namespace {

// one lock covers both tables so a reader never sees new layouts against old roster groups
Lock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;
Table<RosterGroup, kRosterGroupCapacity> g_groups;

[[nodiscard]] std::string_view name_of(const Definition& definition) noexcept {
    return {definition.name.data(), definition.nameLength};
}

[[nodiscard]] bool canonical(const RosterGroup& group) noexcept {
    if (group.registryKey == 0 || group.slotCount == 0 || group.slotCount > kRosterSlotCapacity) {
        return false;
    }
    for (std::size_t slot = 0; slot < kRosterSlotCapacity; ++slot) {
        const bool declared = slot < group.slotCount;
        if (declared
            && (group.slotTypes[slot] == 0 || group.slotTypes[slot] > kMaximumSlotType
                || (group.slotFlags[slot] & ~kSlotFlagMask) != 0
                || group.slotIndices[slot] >= kRosterSlotCapacity)) {
            return false;
        }
        if (declared && slot != 0 && group.slotIndices[slot] <= group.slotIndices[slot - 1]) {
            return false;
        }
        // storage past the declared count must stay zero, or two caches of the same packages could differ
        if (!declared
            && (group.slotTypes[slot] != 0 || group.slotFlags[slot] != 0
                || group.slotIndices[slot] != 0)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool canonical(const Definition& definition, std::size_t groupCount) noexcept {
    if (definition.nameLength == 0 || definition.nameLength > kNameCapacity
        || definition.bubbleCount > kBubbleCapacity || definition.truncated > 1
        || definition.rosterGroupCount > kDestinationGroupCapacity
        || definition.bubbleGroupCount > kDestinationBubbleGroupCapacity
        || definition.spawnStemLength > kSpawnStemCapacity) {
        return false;
    }
    for (std::size_t index = definition.spawnStemLength; index < kSpawnStemCapacity; ++index) {
        if (definition.spawnStem[index] != '\0') {
            return false;
        }
    }
    for (std::size_t group = 0; group < kDestinationGroupCapacity; ++group) {
        const bool declared = group < definition.rosterGroupCount;
        if (declared && definition.rosterGroups[group] >= groupCount) {
            return false;
        }
        if (!declared && definition.rosterGroups[group] != 0) {
            return false;
        }
    }
    for (std::size_t group = 0; group < kDestinationBubbleGroupCapacity; ++group) {
        const bool declared = group < definition.bubbleGroupCount;
        if (declared
            && (definition.bubbleGroups[group] >= groupCount
                || definition.bubbleGroupMasks[group] == 0)) {
            return false;
        }
        if (!declared
            && (definition.bubbleGroups[group] != 0 || definition.bubbleGroupMasks[group] != 0)) {
            return false;
        }
    }
    for (std::size_t index = definition.nameLength; index < kNameCapacity; ++index) {
        if (definition.name[index] != '\0') {
            return false;
        }
    }
    for (std::size_t index = 0; index < kBubbleCapacity; ++index) {
        const bool declared = index < definition.bubbleCount;
        if (declared && definition.bubbleStates[index] != kBubbleEnabledByte
            && definition.bubbleStates[index] != kBubbleDisabledByte) {
            return false;
        }
        if (!declared
            && (definition.bubbleStates[index] != 0 || definition.bubbleHashes[index] != 0
                || definition.bubbleStateCounts[index] != 0
                || definition.bubbleMapIndices[index] != 0)) {
            return false;
        }
    }
    return true;
}

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_definitions.clear();
    g_groups.clear();
}

bool valid(std::span<const Definition> definitions, std::span<const RosterGroup> groups) noexcept {
    if (definitions.size() > kDefinitionCapacity || groups.size() > kRosterGroupCapacity) {
        return false;
    }
    for (std::size_t row = 0; row < groups.size(); ++row) {
        if (!canonical(groups[row])) {
            return false;
        }
    }
    for (std::size_t row = 0; row < definitions.size(); ++row) {
        if (!canonical(definitions[row], groups.size())) {
            return false;
        }
        // a duplicate name would make the destination lookup depend on row order
        for (std::size_t earlier = 0; earlier < row; ++earlier) {
            if (name_of(definitions[earlier]) == name_of(definitions[row])) {
                return false;
            }
        }
    }
    return true;
}

bool replace(std::span<const Definition> definitions,
             std::span<const RosterGroup> groups) noexcept {
    if (!valid(definitions, groups)) {
        return false;
    }
    const Lock::Exclusive guard(g_lock);
    // run both with no short-circuit so the pair cannot be left half replaced
    const bool storedDefinitions = g_definitions.replace(definitions);
    const bool storedGroups = g_groups.replace(groups);
    return storedDefinitions && storedGroups;
}

bool group(std::size_t index, RosterGroup& group) noexcept {
    group = {};
    const Lock::Shared guard(g_lock);
    const std::span<const RosterGroup> rows = g_groups.rows();
    const bool present = index < rows.size();
    if (present) {
        group = rows[index];
    }
    return present;
}

bool group_by_key(std::uint32_t key, std::uint32_t objectTag, RosterGroup& group) noexcept {
    group = {};
    const Lock::Shared guard(g_lock);
    for (const RosterGroup& row : g_groups.rows()) {
        if (row.registryKey == key && (objectTag == 0 || row.objectTag == objectTag)) {
            group = row;
            return true;
        }
    }
    return false;
}

std::size_t group_count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_groups.count();
}

bool snapshot_groups(std::span<RosterGroup> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_groups.snapshot(output, count);
}

bool find(std::string_view name, Definition& definition) noexcept {
    definition = {};
    if (name.empty() || name.size() > kNameCapacity) {
        return false;
    }
    const Lock::Shared guard(g_lock);
    for (const Definition& row : g_definitions.rows()) {
        if (name_of(row) == name) {
            definition = row;
            return true;
        }
    }
    return false;
}

bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.snapshot(output, count);
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.count();
}

} // namespace dawn::state::build_data::scenarios
