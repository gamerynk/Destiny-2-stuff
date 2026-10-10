#include <array>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "../../../middleware/content/packages/tables/roster_intersection.h"
#include "../../../middleware/content/packages/tables/scenario_reader.h"
#include "internal.h"

namespace dawn::client::content::scenarios {
namespace {

namespace tables = middleware::content::packages::tables;

constexpr std::array<std::size_t, 3> kRegistryDescriptors = {
    tables::kRegistryFirstDescriptor,
    tables::kRegistrySecondDescriptor,
    tables::kRegistryThirdDescriptor,
};

constexpr std::size_t kRosterProgressInterval = 64;

void report_progress(std::size_t walked, std::size_t total, std::size_t groups) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=build_data stage=roster walked=%zu of=%zu groups=%zu",
                                      walked,
                                      total,
                                      groups);
    if (written > 0) {
        core::log::write(core::log::Channel::state,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

constexpr std::uint8_t kSlotTypeParticipation = 13;
constexpr std::uint8_t kSlotTypeLifetime = 17;

void note_candidate(Walk& walk,
                    const RosterStorage& storage,
                    std::uint16_t group,
                    bool primary) noexcept {
    for (std::size_t index = 0; index < walk.candidateCount; ++index) {
        if (walk.candidates[index].group == group) {
            walk.candidates[index].primaryRegistry =
                walk.candidates[index].primaryRegistry || primary;
            return;
        }
    }
    if (walk.candidateCount == walk.candidates.size()) {
        return;
    }
    const layouts::RosterGroup& row = storage.groups[group];
    Candidate& candidate = walk.candidates[walk.candidateCount++];
    candidate.group = group;
    candidate.key = row.registryKey;
    candidate.primaryRegistry = primary;
    for (std::size_t slot = 0; slot < row.slotCount; ++slot) {
        candidate.bindsPlayer =
            candidate.bindsPlayer || row.slotTypes[slot] == kSlotTypeParticipation;
        candidate.reportsLifetime =
            candidate.reportsLifetime || row.slotTypes[slot] == kSlotTypeLifetime;
    }
}

[[nodiscard]] bool walk_registry(const reader::Source& source,
                                 reader::Scratch& scratch,
                                 RosterStorage& storage,
                                 Walk& walk,
                                 std::uint32_t sliceSetIndex) noexcept {
    for (std::size_t descriptor = 0; descriptor < kRegistryDescriptors.size(); ++descriptor) {
        tables::Array objects{};
        if (!tables::registry_objects(
                storage.registry, kRegistryDescriptors[descriptor], objects)) {
            continue;
        }
        for (std::uint64_t index = 0; index < objects.count; ++index) {
            std::uint32_t objectTag = 0;
            if (!tables::registry_object_at(storage.registry, objects, index, objectTag)) {
                return false;
            }
            std::uint16_t group = kNotARosterGroup;
            if (!resolve_object(source, scratch, storage, objectTag, group)) {
                return false;
            }
            if (group == kNotARosterGroup) {
                continue;
            }
            note_candidate(walk, storage, group, descriptor == 0);
            if (!tables::observe_roster_key(
                    walk.intersection, sliceSetIndex, storage.groups[group].registryKey)) {
                return true;
            }
        }
    }
    return true;
}

[[nodiscard]] bool walk_destination(const reader::Source& source,
                                    reader::Scratch& scratch,
                                    RosterStorage& storage,
                                    Walk& walk) noexcept {
    tables::Array bubbles{};
    if (!tables::scenario_bubbles(storage.scenario, bubbles)) {
        return false;
    }
    for (std::uint64_t bubbleIndex = 0; bubbleIndex < bubbles.count; ++bubbleIndex) {
        tables::Bubble bubble{};
        if (!tables::bubble_at(storage.scenario, bubbles, bubbleIndex, bubble)) {
            return false;
        }
        for (std::uint64_t stateIndex = 0; stateIndex < bubble.stateCount; ++stateIndex) {
            tables::SliceState state{};
            if (!tables::slice_state_at(storage.scenario, bubble, stateIndex, state)) {
                return false;
            }
            tables::SliceEntry entry{};
            ++storage.reads;
            if (!reader::read_tag(source, scratch, state.entryTag, storage.entry)
                || !tables::slice_entry(storage.entry, entry)) {
                // the destination still transitions into that slice set and no key can be proved present, so none stay safe
                tables::observe_unresolved_slice_set(walk.intersection);
                continue;
            }
            const std::uint32_t sliceSetIndex = entry.index * tables::kSliceSetIndexFactor;
            if (!tables::observe_slice_set(walk.intersection, sliceSetIndex)) {
                return true;
            }
            ++storage.reads;
            if (!reader::read_tag(source, scratch, entry.registryTag, storage.registry)) {
                tables::observe_unresolved_slice_set(walk.intersection);
                continue;
            }
            if (!walk_registry(source, scratch, storage, walk, sliceSetIndex)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool build_rosters(const reader::Source& source,
                   reader::Scratch& scratch,
                   RosterStorage& storage,
                   std::span<layouts::Definition> rows) noexcept {
    storage.reads = 0;
    while (storage.cursor < rows.size() && storage.reads < kRosterReadBudget) {
        if (storage.cursor % kRosterProgressInterval == 0) {
            report_progress(storage.cursor, rows.size(), storage.groupCount);
        }
        layouts::Definition& row = rows[storage.cursor];
        ++storage.cursor;
        row.rosterGroupCount = 0;
        row.rosterGroups = {};
        row.bubbleGroupCount = 0;
        row.bubbleGroups = {};
        row.bubbleGroupMasks = {};
        ++storage.reads;
        if (!reader::read_tag(source, scratch, row.tag, storage.scenario)) {
            continue;
        }
        Walk walk{};
        if (!walk_destination(source, scratch, storage, walk)) {
            continue;
        }
        publish_groups(walk, row);
    }
    return storage.cursor >= rows.size();
}

} // namespace dawn::client::content::scenarios
