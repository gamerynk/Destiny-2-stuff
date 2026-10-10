#include <algorithm>
#include <cstddef>
#include <string_view>

#include "../../../middleware/content/packages/tables/roster_intersection.h"
#include "internal.h"

namespace dawn::client::content::scenarios {
namespace {

namespace tables = middleware::content::packages::tables;

constexpr std::uint32_t kOmegaOpeningRosterKey = 0xD00142CF;

// player-binding or lifetime-reporting groups first, then the destination's own registry, then lower key
[[nodiscard]] bool publishes_first(const Candidate& left, const Candidate& right) noexcept {
    const bool leftFilled = left.bindsPlayer || left.reportsLifetime;
    const bool rightFilled = right.bindsPlayer || right.reportsLifetime;
    if (leftFilled != rightFilled) {
        return leftFilled;
    }
    if (left.primaryRegistry != right.primaryRegistry) {
        return left.primaryRegistry;
    }
    return left.key < right.key;
}

void publish_top_level(Walk& walk, layouts::Definition& row) noexcept {
    std::array<std::uint32_t, tables::kRosterKeyCapacity> safe{};
    std::size_t safeCount = 0;
    if (!tables::safe_roster_keys(walk.intersection, safe, safeCount) || safeCount == 0) {
        return;
    }
    std::array<Candidate, tables::kRosterKeyCapacity> kept{};
    std::size_t keptCount = 0;
    const std::string_view destination(row.name.data(), row.nameLength);
    for (std::size_t index = 0; index < walk.candidateCount; ++index) {
        const Candidate& candidate = walk.candidates[index];
        const auto last = safe.begin() + static_cast<std::ptrdiff_t>(safeCount);
        const bool presentInEverySlice = std::find(safe.begin(), last, candidate.key) != last;
        // the opening group exists only in Omega slice 120; keep it in the cached row, the runtime
        // publisher filters it out elsewhere
        const bool omegaOpening = destination == "mission_scot"
                                  && candidate.key == kOmegaOpeningRosterKey;
        if ((presentInEverySlice || omegaOpening) && keptCount < kept.size()) {
            kept[keptCount++] = candidate;
        }
    }
    std::sort(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(keptCount), publishes_first);
    // a roster missing either filled type seeds nothing the spawn gate reads, so publish none
    bool binds = false;
    bool reports = false;
    for (std::size_t index = 0; index < keptCount; ++index) {
        binds = binds || kept[index].bindsPlayer;
        reports = reports || kept[index].reportsLifetime;
    }
    if (!binds || !reports) {
        return;
    }
    const std::size_t published = (std::min)(keptCount, layouts::kDestinationGroupCapacity);
    for (std::size_t index = 0; index < published; ++index) {
        row.rosterGroups[index] = kept[index].group;
    }
    row.rosterGroupCount = static_cast<std::uint8_t>(published);
}

void publish_per_bubble(Walk& walk, layouts::Definition& row) noexcept {
    std::array<std::uint32_t, tables::kRosterKeyCapacity> keys{};
    std::array<std::uint64_t, tables::kRosterKeyCapacity> masks{};
    std::size_t partialCount = 0;
    if (!tables::partial_roster_keys(walk.intersection, keys, masks, partialCount)
        || partialCount == 0) {
        return;
    }
    const std::string_view destination(row.name.data(), row.nameLength);
    std::size_t published = 0;
    for (std::size_t index = 0;
         index < walk.candidateCount && published < layouts::kDestinationBubbleGroupCapacity;
         ++index) {
        const Candidate& candidate = walk.candidates[index];
        // keep the Omega script-baseline gate in the top-level publisher; runtime filters it by slice 120 and host readiness
        if (destination == "mission_scot" && candidate.key == kOmegaOpeningRosterKey) {
            continue;
        }
        for (std::size_t partial = 0; partial < partialCount; ++partial) {
            if (keys[partial] != candidate.key) {
                continue;
            }
            row.bubbleGroups[published] = candidate.group;
            row.bubbleGroupMasks[published] = masks[partial];
            ++published;
            break;
        }
    }
    row.bubbleGroupCount = static_cast<std::uint8_t>(published);
}

} // namespace

void publish_groups(Walk& walk, layouts::Definition& row) noexcept {
    row.rosterGroupCount = 0;
    row.rosterGroups = {};
    row.bubbleGroupCount = 0;
    row.bubbleGroups = {};
    row.bubbleGroupMasks = {};
    publish_top_level(walk, row);
    publish_per_bubble(walk, row);
}

} // namespace dawn::client::content::scenarios
