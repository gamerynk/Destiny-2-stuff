#include <array>
#include <algorithm>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../state/activity/crucible/native_maps.h"
#include "../../../../../state/activity/crucible/native_modes.h"
#include "../../../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../../../state/build_data/runtime.h"
#include "internal.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

constexpr std::array<const char*, 5> kOutcomeNames = {
    "ok", "no_epoch", "no_layout", "no_groups", "encode"};

void report_crucible_catalog(const Session& session,
                             const message::Roster& roster,
                             std::string_view destination) noexcept {
    namespace catalog = state::activity::crucible;
    const auto* map = catalog::find_native_map(destination);
    if (map == nullptr) return;
    state::build_data::scenarios::Definition installed{};
    if (!state::build_data::find_scenario_layout(destination, installed)
        || installed.tag != map->scenarioTag) {
        return;
    }
    const auto registered_spawns = [&roster](auto spawns, std::uint8_t slotType) noexcept {
        std::size_t registered = 0;
        for (const auto& spawn : spawns) {
            bool found = false;
            for (std::size_t groupIndex = 0; !found && groupIndex < roster.groupCount; ++groupIndex) {
                const auto& group = roster.groups[groupIndex];
                if (group.key != spawn.registryKey) continue;
                for (std::size_t slot = 0; slot < group.slotTypes.size(); ++slot) {
                    if (slot < group.slotIndices.size() && group.slotTypes[slot] == slotType
                        && group.slotIndices[slot] == spawn.slotIndex) {
                        found = true;
                        break;
                    }
                }
            }
            registered += found ? 1U : 0U;
        }
        return registered;
    };
    const auto registered = registered_spawns(catalog::combat_spawns(*map), 50);
    const auto registeredTeam = registered_spawns(catalog::team_spawns(*map), 49);
    state::activity::destination::DestinationSelection selection{};
    const bool selected = state::activity::destination::snapshot(
        session.activitySessionId, selection);
    const auto* sourceMode = selected
        ? catalog::find_native_mode(selection.previousActivityIndex) : nullptr;
    const auto* currentMode = selected
        ? catalog::find_native_mode(selection.activityIndex) : nullptr;
    const auto* mode = currentMode != nullptr ? currentMode : sourceMode;
    const std::string_view symbol = mode == nullptr || mode->symbolicName.empty()
        ? std::string_view{"unresolved"} : mode->symbolicName;
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(), line.size(),
        "ev=pvp stage=catalog session=0x%llX map=\"%.*s\" scenario=0x%08X "
        "spawns_registered=%zu spawns_authored=%zu team_spawns_registered=%zu "
        "team_spawns_authored=%zu activity=%d previous_activity=%d "
        "mode_reference=%.*s mode_reference_source=%s policy=0x%08X options=0x%08X",
        static_cast<unsigned long long>(session.activitySessionId),
        static_cast<int>(map->displayName.size()), map->displayName.data(), map->scenarioTag,
        registered, map->combatSpawnCount, registeredTeam, map->teamSpawnCount,
        selected ? selection.activityIndex : -1, selected ? selection.previousActivityIndex : -1,
        static_cast<int>(symbol.size()), symbol.data(),
        currentMode != nullptr ? "activity" : sourceMode != nullptr ? "previous_activity" : "none",
        mode != nullptr ? mode->policyTag : 0U, mode != nullptr ? mode->optionsTag : 0U);
    if (written > 0) {
        core::log::write(core::log::Channel::server, core::log::Level::info,
            {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

} // namespace

void report_roster_push(Session& session,
                        const message::Snapshot& snapshot,
                        std::string_view destination,
                        std::size_t bytes,
                        std::int32_t grant,
                        RosterOutcome outcome) noexcept {
    const message::Roster& roster = snapshot.roster;
    if (outcome == RosterOutcome::published && destination.starts_with("pvp_")
        && session.activityRosterLoggedTeam != snapshot.playerAssignment + 1U) {
        session.activityRosterLoggedTeam = static_cast<std::uint8_t>(snapshot.playerAssignment + 1U);
        report_crucible_catalog(session, roster, destination);
        std::array<char, core::log::kLineCapacity> teamLine{};
        const int teamWritten = std::snprintf(
            teamLine.data(),
            teamLine.size(),
            "ev=pvp stage=team session=0x%llX account=0x%llX platform=0x%llX player=0x%llX "
            "dest=%.*s assignment=%u team_index=%u",
            static_cast<unsigned long long>(session.activitySessionId),
            static_cast<unsigned long long>(session.credentials.accountSoid),
            static_cast<unsigned long long>(session.credentials.platformId),
            static_cast<unsigned long long>(snapshot.playerKey),
            static_cast<int>(destination.size()),
            destination.data(),
            static_cast<unsigned>(snapshot.playerAssignment),
            static_cast<unsigned>(snapshot.playerTeamIndex));
        if (teamWritten > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::info,
                             {teamLine.data(), static_cast<std::size_t>(teamWritten)});
        }
    }
    const auto reason = static_cast<std::uint8_t>(outcome) + 1U;
    if (outcome != RosterOutcome::published && session.activityRosterReason == reason) {
        return;
    }
    session.activityRosterReason = static_cast<std::uint8_t>(reason);
    std::size_t slots = 0;
    std::size_t seeded = 0;
    for (std::size_t index = 0; index < roster.groupCount; ++index) {
        slots += roster.groups[index].slotTypes.size();
        seeded += static_cast<std::size_t>(std::count_if(roster.groups[index].slotFlags.begin(),
                                                        roster.groups[index].slotFlags.end(),
                                                        [](std::uint8_t flags) {
                                                            return flags != 0;
                                                        }));
    }
    std::array<char, core::log::kLineCapacity> line{};
    const int written =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=activity stage=roster result=%s soid=0x%llX foreign=%u dest=%.*s "
                      "groups=%zu top=%zu sub=%zu objects=%zu seeded=%zu bytes=%zu "
                      "state=%u phase=%s client_sync=%s activity_join=%u "
                      "keygroup=0x%X grant=%d "
                      "region=%u slice=%u spawn=0x%X director=%u director_active=%u "
                      "director_transition=%u script_flag=%u script_state=%d "
                      "join=0x%llX player=0x%llX assignment=%u team_index=%u control_placements=%u",
                      kOutcomeNames[static_cast<std::size_t>(outcome)],
                      static_cast<unsigned long long>(session.activitySessionId),
                      session.activityJoinedForeignSession ? 1U : 0U,
                      static_cast<int>(destination.size()),
                      destination.data(),
                      roster.groupCount,
                      roster.topLevelGroupCount,
                      roster.bubbleSubBlocks.size(),
                      slots,
                      seeded,
                      bytes,
                      session.activityRosterState,
                      snapshot.phaseOneOnly ? "register" : "seed",
                      snapshot.awaitClientSync ? "hold" : "ready",
                      snapshot.allowJoinInProgress ? 1U : 0U,
                      roster.playerKeyGroup,
                      grant,
                      snapshot.region,
                      snapshot.spawnSliceSet,
                      snapshot.spawnSetHash,
                      static_cast<unsigned>(snapshot.missionDirectorVariant),
                      snapshot.missionDirectorActive ? 1U : 0U,
                      snapshot.missionDirectorTransition ? 1U : 0U,
                      snapshot.activityScriptFlag ? 1U : 0U,
                      snapshot.activityScriptState,
                      static_cast<unsigned long long>(session.activityCharacterSoid),
                      static_cast<unsigned long long>(snapshot.playerKey),
                      static_cast<unsigned>(snapshot.playerAssignment),
                      static_cast<unsigned>(snapshot.playerTeamIndex),
                      snapshot.cruciblePlacements.enabled ? 1U : 0U);
    if (written > 0) {
        const bool firstForeignPublication =
            outcome == RosterOutcome::published && session.activityJoinedForeignSession
            && session.activityRosterSends == 1;
        const bool clientSyncTransition =
            outcome == RosterOutcome::published && session.activityRosterStaged.staged
            && session.activityRosterStaged.priorClientSyncKnown
            && session.activityRosterStaged.priorAwaitClientSync
                   != snapshot.awaitClientSync;
        core::log::write(core::log::Channel::server,
                         outcome == RosterOutcome::published
                             ? (firstForeignPublication || clientSyncTransition
                                    ? core::log::Level::info
                                    : core::log::Level::debug)
                             : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace dawn::server::bap::encrypted::push::activity
