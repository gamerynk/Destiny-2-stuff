#include "mission_roster.h"

#include <array>

#include "../../../../../state/build_data/runtime.h"
#include "../../../../../state/build_data/scenarios/mission_roster_pins.h"

namespace dawn::server::bap::encrypted::push::activity::mission_roster {
namespace {

namespace layouts = state::build_data::scenarios;
namespace pins = layouts::mission_roster_pins;

constexpr std::uint8_t kSlotTypeParticipation = 13;

struct Entry final {
    std::uint32_t key{};
    std::uint16_t tableIndex{};
    std::uint32_t objectTag{};
    std::uint64_t bubbleMask{};
    bool fromRow{};
};

constexpr std::uint16_t kNotARow = 0xFFFF;

[[nodiscard]] bool add(std::array<Entry, message::kGroupCapacity>& entries,
                       std::size_t& count,
                       const Entry& entry,
                       bool topLevel,
                       std::size_t topLevelCount,
                       Report& report) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (entries[index].key != entry.key) {
            continue;
        }
        if (!topLevel && index >= topLevelCount) {
            entries[index].bubbleMask |= entry.bubbleMask;
        }
        return true;
    }
    if (count == entries.size()) {
        ++report.full;
        return false;
    }
    entries[count++] = entry;
    return true;
}

} // namespace

bool compose(const layouts::Definition& layout,
             std::string_view destination,
             Scratch& scratch,
             message::Roster& roster,
             Report& report) noexcept {
    roster = {};
    report = {};
    if (!pins::has_pins(destination) || layout.rosterGroupCount == 0) {
        return false;
    }
    std::array<Entry, message::kGroupCapacity> entries{};
    std::size_t count = 0;

    for (std::size_t index = 0; index < layout.rosterGroupCount; ++index) {
        layouts::RosterGroup& probe = scratch.rosterGroups[0];
        if (!state::build_data::find_roster_group(layout.rosterGroups[index], probe)) {
            return false;
        }
        if (!add(entries, count, {probe.registryKey, layout.rosterGroups[index], 0, 0, true},
                 true, count, report)) {
            return false;
        }
        ++report.destination;
    }
    for (const pins::Pin& pin : pins::kPins) {
        if (pin.destination != destination || !pin.topLevel) {
            continue;
        }
        if (!add(entries, count, {pin.key, kNotARow, pin.objectTag, 0, false}, true, count,
                 report)) {
            return false;
        }
        ++report.pinned;
    }
    const std::size_t topLevelCount = count;

    for (std::size_t index = 0; index < layout.bubbleGroupCount; ++index) {
        layouts::RosterGroup& probe = scratch.rosterGroups[0];
        if (!state::build_data::find_roster_group(layout.bubbleGroups[index], probe)) {
            return false;
        }
        if (!add(entries, count,
                 {probe.registryKey, layout.bubbleGroups[index], 0,
                  layout.bubbleGroupMasks[index], true},
                 false, topLevelCount, report)) {
            return false;
        }
        ++report.destination;
    }
    for (const pins::Pin& pin : pins::kPins) {
        if (pin.destination != destination || pin.topLevel || pin.bubble >= layouts::kBubbleCapacity) {
            continue;
        }
        if (!add(entries, count,
                 {pin.key, kNotARow, pin.objectTag, std::uint64_t{1} << pin.bubble, false}, false,
                 topLevelCount, report)) {
            return false;
        }
        ++report.pinned;
    }

    for (std::size_t index = 0; index < count; ++index) {
        layouts::RosterGroup& group = scratch.rosterGroups[index];
        const Entry& entry = entries[index];
        const bool found =
            entry.fromRow
                ? state::build_data::find_roster_group(entry.tableIndex, group)
                : state::build_data::find_roster_group_by_key(entry.key, entry.objectTag, group);
        if (!found || group.slotCount == 0 || group.slotCount > group.slotTypes.size()) {
            ++report.missing;
            report.lastMissing = entry.key;
            continue;
        }
        roster.groups[index] = {
            group.registryKey,
            std::span<const std::uint8_t>(group.slotTypes.data(), group.slotCount),
            std::span<const std::uint8_t>(group.slotFlags.data(), group.slotCount),
            std::span<const std::uint16_t>(group.slotIndices.data(), group.slotCount),
        };
    }
    if (report.missing != 0) {
        roster = {};
        return false;
    }
    roster.groupCount = count;
    roster.topLevelGroupCount = topLevelCount;

    std::size_t published = 0;
    for (std::size_t bubble = 0; bubble < scratch.rosterSubBlocks.size(); ++bubble) {
        std::size_t keyCount = 0;
        for (std::size_t index = topLevelCount; index < count; ++index) {
            if ((entries[index].bubbleMask & (std::uint64_t{1} << bubble)) == 0) {
                continue;
            }
            if (keyCount == scratch.rosterSubBlockKeys[published].size()) {
                ++report.full;
                roster = {};
                return false;
            }
            scratch.rosterSubBlockKeys[published][keyCount++] = entries[index].key;
        }
        if (keyCount == 0) {
            continue;
        }
        scratch.rosterSubBlocks[published].bubble = static_cast<std::uint32_t>(bubble);
        scratch.rosterSubBlocks[published].keys =
            std::span<const std::uint32_t>(scratch.rosterSubBlockKeys[published].data(), keyCount);
        ++published;
    }
    roster.bubbleSubBlocks = std::span(scratch.rosterSubBlocks).first(published);

    for (std::size_t index = 0; index < topLevelCount && roster.playerKeyGroup == 0; ++index) {
        const layouts::RosterGroup& group = scratch.rosterGroups[index];
        for (std::size_t slot = 0; slot < group.slotCount; ++slot) {
            if (group.slotTypes[slot] == kSlotTypeParticipation) {
                roster.playerKeyGroup = group.registryKey;
                break;
            }
        }
    }
    if (roster.playerKeyGroup == 0) {
        roster = {};
        return false;
    }
    return true;
}

} // namespace dawn::server::bap::encrypted::push::activity::mission_roster
