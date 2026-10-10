#include <Windows.h>
#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../../../../RestorationServer/src/account_store.h"
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

#include "../../../../../state/account/account_state.h"
#include "../../../../../core/logging/log.h"
#include "../../../../../state/activity/runtime.h"
#include "../../../../../state/activity/shared/runtime.h"
#include "../../../../../state/activity/mission_host/mission_host.h"
#include "../../../../../state/activity/strike_pact/runtime.h"
#include "../../../../../state/activity/defaults/activity_defaults_snapshot.h"
#include "../../../../../state/activity/crucible/native_modes.h"
#include "../../../../../state/activity/crucible/match_runtime.h"
#include "../../../../../state/activity/crucible/native_score_headline.h"
#include "../../../../../state/activity/crucible/completion_timing.h"
#include "../../../../../state/activity/crucible/native_countdown_publication.h"
#include "../../../../../state/activity/crucible/objective_controller.h"
#include "../../../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../../../state/activity/destination/activity_destination_spawn_binding.h"
#include "../../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../../state/build_data/runtime.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../../../gameplay/group/group_host_sessions.h"
#include "activity_arrival.h"
#include "activity_join_policy.h"
#include "activity_roster_generation_policy.h"
#include "activity_spawn_sync.h"
#include "../../../../../middleware/bap/activity_message/activity_player_assignment.h"
#include "../../../../../middleware/bap/activity_message/native/activity_clock_authority.h"
#include "internal.h"
#include "mission_roster.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

namespace layouts = state::build_data::scenarios;

// only 3, 6 and 10 are safe: spawn gate G4 indexes a jump table with no bounds check, so any other value is a wild jump
// 3 is what a live activity measured
constexpr std::uint8_t kLifetimeState = 3;
// the state byte is stored biased into one signed byte, so the sequence stays inside this
constexpr std::uint8_t kStateSequenceWrap = 128;
constexpr std::uint32_t kFoldBasis = 2166136261U;
constexpr std::uint32_t kFoldPrime = 16777619U;
// only a type-13 slot binds the player, so only a group holding one may carry the key
constexpr std::uint8_t kSlotTypeParticipation = 13;
// the join request names its character in the low half of the SOID, so compare on that half
constexpr std::uint64_t kIdentityLowMask = 0xFFFFFFFFULL;
// Omega's opening actors, gate controller and trigger volumes exist only in slice-set 120
constexpr std::uint32_t kOmegaOpeningRosterKey = 0xD00142CF;
constexpr std::int32_t kOmegaOpeningRegion = 120;

// the client sends a short identity form that binds no object, so publish the full authored SOID instead
[[nodiscard]] std::uint64_t roster_player_key(std::uint64_t joinCharacter) noexcept {
    const state::AccountState account = state::account_snapshot();
    const std::uint64_t selected = state::account::selected_character_soid(account);
    if (joinCharacter == 0) {
        return selected;
    }
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        const std::uint64_t soid = account.characters[index].soid;
        if ((soid & kIdentityLowMask) == (joinCharacter & kIdentityLowMask)) {
            return soid;
        }
    }
    return selected;
}

[[nodiscard]] bool fill_group(std::uint16_t tableIndex,
                              Scratch& scratch,
                              std::size_t slot,
                              message::Roster& roster) noexcept {
    layouts::RosterGroup& group = scratch.rosterGroups[slot];
    if (!state::build_data::find_roster_group(tableIndex, group)) {
        return false;
    }
    roster.groups[slot].key = group.registryKey;
    roster.groups[slot].slotTypes =
        std::span<const std::uint8_t>(group.slotTypes.data(), group.slotCount);
    roster.groups[slot].slotFlags =
        std::span<const std::uint8_t>(group.slotFlags.data(), group.slotCount);
    roster.groups[slot].slotIndices =
        std::span<const std::uint16_t>(group.slotIndices.data(), group.slotCount);
    return true;
}

[[nodiscard]] std::span<const message::BubbleSubBlock> fill_sub_blocks(
    const layouts::Definition& layout, Scratch& scratch, const message::Roster& roster) noexcept {
    std::size_t published = 0;
    for (std::size_t bubble = 0; bubble < scratch.rosterSubBlocks.size(); ++bubble) {
        std::size_t keyCount = 0;
        for (std::size_t index = 0; index < layout.bubbleGroupCount; ++index) {
            if ((layout.bubbleGroupMasks[index] & (std::uint64_t{1} << bubble)) == 0) {
                continue;
            }
            scratch.rosterSubBlockKeys[published][keyCount] =
                roster.groups[roster.topLevelGroupCount + index].key;
            ++keyCount;
        }
        if (keyCount == 0) {
            continue;
        }
        scratch.rosterSubBlocks[published].bubble = static_cast<std::uint32_t>(bubble);
        scratch.rosterSubBlocks[published].keys = std::span<const std::uint32_t>(
            scratch.rosterSubBlockKeys[published].data(), keyCount);
        ++published;
    }
    return std::span(scratch.rosterSubBlocks).first(published);
}

[[nodiscard]] bool fill_roster(const layouts::Definition& layout,
                               Scratch& scratch,
                               std::string_view destination,
                               std::int32_t region,
                               message::Roster& roster) noexcept {
    roster = {};
    const std::size_t declaredCount =
        std::size_t{layout.rosterGroupCount} + std::size_t{layout.bubbleGroupCount};
    if (layout.rosterGroupCount == 0 || declaredCount > scratch.rosterGroups.size()
        || declaredCount > roster.groups.size()) {
        return false;
    }

    for (std::size_t index = 0; index < layout.rosterGroupCount; ++index) {
        const std::size_t slot = roster.groupCount;
        if (!fill_group(layout.rosterGroups[index], scratch, slot, roster)) {
            return false;
        }
        const layouts::RosterGroup& retained = scratch.rosterGroups[slot];
        if (destination == "mission_scot" && retained.registryKey == kOmegaOpeningRosterKey
            && region != kOmegaOpeningRegion) {
            roster.groups[slot] = {};
            scratch.rosterGroups[slot] = {};
            continue;
        }
        ++roster.groupCount;
    }
    roster.topLevelGroupCount = roster.groupCount;

    for (std::size_t index = 0; index < layout.bubbleGroupCount; ++index) {
        const std::size_t slot = roster.groupCount;
        if (!fill_group(layout.bubbleGroups[index], scratch, slot, roster)) {
            return false;
        }
        ++roster.groupCount;
    }
    roster.bubbleSubBlocks = fill_sub_blocks(layout, scratch, roster);

    for (std::size_t index = 0;
         index < roster.topLevelGroupCount && roster.playerKeyGroup == 0;
         ++index) {
        const layouts::RosterGroup& group = scratch.rosterGroups[index];
        for (std::size_t slot = 0; slot < group.slotCount; ++slot) {
            if (group.slotTypes[slot] == kSlotTypeParticipation) {
                roster.playerKeyGroup = group.registryKey;
                break;
            }
        }
    }
    return roster.playerKeyGroup != 0;
}

[[nodiscard]] std::uint32_t fold_groups(const message::Roster& roster) noexcept {
    std::uint32_t folded = kFoldBasis;
    for (std::size_t index = 0; index < roster.groupCount; ++index) {
        folded = (folded ^ roster.groups[index].key) * kFoldPrime;
    }
    return folded;
}

[[nodiscard]] std::uint8_t
next_state_sequence(Session& session,
                    std::uint32_t folded,
                    bool burst,
                    bool awaitClientSync) noexcept {
    if (roster_generation::should_advance(session.activityRosterSends,
                                          session.activityRosterGroups,
                                          folded,
                                          burst,
                                          session.activityRosterClientSyncKnown,
                                          session.activityRosterAwaitClientSync,
                                          awaitClientSync)) {
        session.activityRosterState =
            static_cast<std::uint8_t>((session.activityRosterState + 1) % kStateSequenceWrap);
        session.activityRosterGroups = folded;
    }
    session.activityRosterSends =
        roster_generation::next_sends(session.activityRosterSends, awaitClientSync);
    session.activityRosterClientSyncKnown = true;
    session.activityRosterAwaitClientSync = awaitClientSync;
    return session.activityRosterState;
}

constexpr std::string_view kStrikeDestination = "strike_pact";
constexpr std::int16_t kGrandmasterActivityIndex = 835;
constexpr std::uint8_t kGrandmasterEnemyVariant = 5;

void report_mission_roster(Session& session,
                           std::string_view destination,
                           bool composed,
                           const mission_roster::Report& report,
                           std::size_t groups) noexcept {
    const std::uint32_t folded = (composed ? 1U : 0U) | (report.missing << 1U)
                                 | (report.full << 9U) | (report.pinned << 17U);
    if (session.activityMissionRosterLogged == folded + 1U) {
        return;
    }
    session.activityMissionRosterLogged = folded + 1U;
    std::array<char, 320> line{};
    const int written = std::snprintf(
        line.data(), line.size(),
        "ev=mission_roster dest=%.*s result=%s session=0x%llX groups=%zu destination=%u pinned=%u "
        "missing=%u full=%u last_missing=%08X",
        static_cast<int>(destination.size()), destination.data(),
        composed ? "ok" : "fallback", static_cast<unsigned long long>(session.activitySessionId),
        groups, report.destination, report.pinned, report.missing, report.full,
        report.lastMissing);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         composed ? core::log::Level::info : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

bool publish_mission(Session& session,
                     const state::activity::destination::DestinationSelection& selection,
                     std::int32_t region,
                     message::Snapshot& snapshot) noexcept {
    namespace host = state::activity::mission_host;
    namespace strike = state::activity::strike_pact;
    const std::uint64_t now = GetTickCount64();
    const std::uint64_t run = host::bind(session.activitySessionId, now);
    if (run == 0) {
        return false;
    }
    host::note_member(session.activitySessionId, session.credentials.platformId);
    host::note_region(session.activitySessionId, region, strike::region_rank);
    if (snapshot.awaitClientSync) {
        session.activityMissionHoldSession = session.activitySessionId;
    } else if (session.activityMissionHoldSession == session.activitySessionId) {
        host::note_arrived(session.activitySessionId);
    }
    if (!strike::prepare(run, true, false)) {
        return false;
    }
    const std::int32_t missionRegion = host::region();
    snapshot.strike_pact =
        strike::snapshot(run, now, true, missionRegion >= 0 ? missionRegion : region);
    snapshot.strike_pact.enemyVariant =
        selection.activityIndex == kGrandmasterActivityIndex ? kGrandmasterEnemyVariant : 0U;
    session.activityRosterStaged.missionRun = run;
    session.activityRosterStaged.missionRevision = snapshot.strike_pact.revision;
    session.activityRosterStaged.mission = true;
    if (!snapshot.strike_pact.enabled) {
        return true;
    }
    snapshot.missionCompletion = snapshot.strike_pact.completion;
    snapshot.gameplayClockTicks = snapshot.strike_pact.activityTime;
    // a death respawns on the section the mission holds, not the destination's opening arrival
    if (snapshot.strike_pact.checkpointSpawnSet != 0
        && snapshot.strike_pact.checkpointSpawnSet != message::kAbsentSpawnSetHash
        && snapshot.strike_pact.checkpointSliceSet <= message::kMaximumSpawnSliceSet) {
        snapshot.spawnSliceSet = snapshot.strike_pact.checkpointSliceSet;
        snapshot.spawnSetHash = snapshot.strike_pact.checkpointSpawnSet;
        snapshot.hasSpawnOverride = true;
    }
    return true;
}

} // namespace

EffectiveRegion effective_region(std::uint64_t sessionId) noexcept {
    state::activity::defaults::ActivityDefaults defaults{};
    state::activity::destination::DestinationSelection selection{};
    state::activity::defaults::snapshot(defaults);
    if (!state::activity::destination::snapshot(sessionId, selection)) {
        selection = defaults.defaultDestination.selection;
    }
    const std::string_view name(reinterpret_cast<const char*>(selection.packageName.data()),
                                selection.packageNameLength);
    layouts::Definition layout{};
    static_cast<void>(state::build_data::find_scenario_layout(name, layout));
    EffectiveRegion region{};
    region.arrival = arrival_slice_set(defaults.defaultDestination, selection, name, layout);
    const std::int32_t reported = state::activity::membership::reported_region(sessionId);
    region.reported = reported >= 0;
    region.index = region.reported ? reported : static_cast<std::int32_t>(region.arrival);
    return region;
}

EffectiveRegion planned_region(const state::activity::membership::PendingMutation& mutation,
                               std::uint64_t sessionId) noexcept {
    EffectiveRegion region = effective_region(sessionId);
    // same rule the State merge uses so body and record agree; a negative index is the unset value the client sends on its way out
    if (mutation.authoritativeInput.hasRegion
        && mutation.authoritativeInput.region.index
               > state::activity::membership::kAbsentRegionIndex) {
        region.index = mutation.authoritativeInput.region.index;
        region.reported = true;
    }
    return region;
}

RosterOutcome build_roster_snapshot(Session& session,
                                    Scratch& scratch,
                                    message::Snapshot& snapshot,
                                    std::span<char> destination,
                                    std::size_t& destinationLength,
                                    bool burst,
                                    const core::protocol::ActivityPlayerTeam* preparedTeam) noexcept {
    snapshot = {};
    destinationLength = 0;
    state::activity::defaults::ActivityDefaults defaults{};
    state::activity::destination::DestinationSelection selection{};
    state::activity::defaults::snapshot(defaults);
    // never guess from the newest process-wide session; an older public client may still be alive during the crossing
    std::uint64_t regionFrom = session.activitySessionId;
    if (session.activityJoinedForeignSession
        && state::activity::membership::reported_region(regionFrom) < 0) {
        const std::uint64_t source =
            server::gameplay::group::source_activity_session_for_host(
                session.activitySessionId);
        if (source != state::activity::kAbsentSessionId) {
            regionFrom = source;
        }
    }
    if (!state::activity::destination::snapshot(session.activitySessionId, selection)) {
        selection = defaults.defaultDestination.selection;
    }
    layouts::Definition layout{};
    const std::string_view name(reinterpret_cast<const char*>(selection.packageName.data()),
                                selection.packageNameLength);
    destinationLength = (std::min)(name.size(), destination.size());
    std::copy_n(name.begin(), destinationLength, destination.begin());
    if (!state::build_data::find_scenario_layout(name, layout)) {
        return RosterOutcome::noLayout;
    }
    const state::activity::defaults::FallbackPolicy& fallback =
        defaults.defaultDestination.fallback;
    // one resolution serves this body and the citizen advertisement in message 12; two would land the join descriptor in a region the client isn't pending on
    const EffectiveRegion region = effective_region(regionFrom);
    const bool missionDestination =
        name == kStrikeDestination && !session.activityJoinedForeignSession;
    bool missionRoster = false;
    if (missionDestination) {
        mission_roster::Report missionReport{};
        missionRoster =
            mission_roster::compose(layout, name, scratch, snapshot.roster, missionReport);
        report_mission_roster(session, name, missionRoster, missionReport,
                              snapshot.roster.groupCount);
    }
    if (!missionRoster
        && !fill_roster(layout,
                        scratch,
                        name,
                        region.index,
                        snapshot.roster)) {
        return RosterOutcome::noGroups;
    }
    snapshot.patchEpoch = session.activityPatchEpoch;
    namespace assignments=middleware::bap::activity_message::player_assignment;
    const auto* selectedMode=state::activity::crucible::find_native_mode(selection.previousActivityIndex);
    std::uint64_t account{};
    const auto context=state::capture_account_runtime(account);
    state::activity::SessionRecord localRecord{};
    const bool haveLocal=state::activity::snapshot_for_runtime(context,account,regionFrom,localRecord);
    const auto team=assignments::resolve(assignments::competitive_destination(name),
        selectedMode?assignments::mode_for(selectedMode->symbolicName):assignments::Mode::legacy,
        preparedTeam?*preparedTeam:state::activity::membership::player_team(session.activitySessionId),
        haveLocal?localRecord.sharedMemberSlot:32);
    snapshot.playerAssignment=team.faction;
    snapshot.playerTeamIndex=team.teamIndex;
    // set for derived hosts too; they must not restore the skip-delay bit just because only the private activity publishes the score panel
    snapshot.honorNativeRespawnDelay=assignments::competitive_destination(name)
        && selectedMode && (assignments::mode_for(selectedMode->symbolicName)==assignments::Mode::clash
                         || assignments::mode_for(selectedMode->symbolicName)==assignments::Mode::rumble);
    // the client binds its player by matching this against the object registry, and the short form the join carries matches nothing
    snapshot.playerKey = roster_player_key(session.activityCharacterSoid);
    // the old encoder documents this key as message 12's member record +16 but sends the character SOID; that field is the membership identity
    if (defaults.rosterKeyFromIdentity) {
        const std::uint64_t identity =
            state::activity::membership::join_identity(session.activitySessionId);
        if (identity != 0) {
            snapshot.playerKey = identity;
        }
    }
    snapshot.lifetime = kLifetimeState;
    // hold type-13 until the private activity's native membership is acknowledged; account preference writes don't establish world readiness
    snapshot.awaitClientSync =
        spawn_sync::awaiting_client_sync(session.credentials.platformId);
    session.activityRosterSpawnSyncRevision =
        spawn_sync::revision(session.credentials.platformId);
    snapshot.allowJoinInProgress = activity_allows_join_in_progress(
        region.reported, region.index, session.activityRosterSpawnSyncRevision,
        snapshot.awaitClientSync);
    if(!session.activityJoinedForeignSession) {
        state::activity::crucible::host::View match{};
        if(state::activity::crucible::runtime::publication(session.activitySessionId,
            region.reported && !snapshot.awaitClientSync,match)) {
            const bool rumble=match.mode==state::activity::crucible::match_rules::Mode::rumble;
            snapshot.competitiveScoreboard=true;
            snapshot.completeScoreboardRoster=true;
            snapshot.scoreboardStats=true;
            snapshot.scoreboardLayout=0x5DEA3FF8U;
            snapshot.nativeClockVisible = match.phase == state::activity::crucible::host::Phase::playing
                || match.phase == state::activity::crucible::host::Phase::results;
            namespace clock = middleware::bap::activity_message::native::activity_clock;
            snapshot.nativeCountdownRunning = match.phase == state::activity::crucible::host::Phase::playing;
            const auto remaining = snapshot.nativeCountdownRunning ? match.durationMs : match.remainingMs;
            snapshot.nativeCountdown = clock::from_milliseconds(match.durationMs, snapshot.nativeCountdownDurationTicks)
                && clock::from_milliseconds(remaining, snapshot.nativeCountdownRemainingTicks)
                && clock::from_milliseconds(match.elapsedMs, snapshot.gameplayClockTicks);
            snapshot.teamFaction=message::kFirstCrucibleFaction+snapshot.playerTeamIndex;
            snapshot.completeParticipationRoster = true;
            snapshot.unresolvedParticipationRoster = true;
            state::AccountRuntimeContext owner{};
            state::activity::SessionRecord root{};
            if (state::activity::shared::canonical_context(session.activitySessionId, owner)
                && state::activity::snapshot_for_runtime(owner, localRecord.sharedOwnerAccountSoid,
                    session.activitySessionId, root)) {
                std::array<std::uint64_t,32> activeAccounts{};
                bool complete = state::activity::shared::participant_accounts_snapshot(root, activeAccounts);
                std::array<bool,12> assigned{};
                for (std::size_t i=0; complete && i<match.count; ++i) {
                    if (std::find(activeAccounts.begin(), activeAccounts.end(), match.rows[i].account)
                        == activeAccounts.end()) continue;
                    state::activity::SessionRecord peer{};
                    if (!state::activity::shared::participant_snapshot(root, match.rows[i].account, peer)
                        || peer.sharedMemberSlot >= 12 || assigned[peer.sharedMemberSlot]
                        || !peer.membership.hasIdentity
                        || peer.membership.identity.opaqueSoid != match.rows[i].player) {
                        complete = false; break;
                    }
                    assigned[peer.sharedMemberSlot] = true;
                    const auto side = match.rows[i].side;
                    auto& player = snapshot.participationPlayers[peer.sharedMemberSlot];
                    player.key = peer.membership.identity.opaqueSoid;
                    player.region = peer.membership.region.index;
                    player.assignment = side;
                    player.faction = static_cast<std::uint8_t>(message::kFirstCrucibleFaction + side);
                    std::uint64_t platform{};
                    if (match.rows[i].account == session.credentials.accountSoid)
                        platform = session.credentials.platformId;
#if defined(RESTORATION_DEDICATED_SERVER)
                    else {
                        std::array<char,::restoration::account_store::kAccountNameCapacity> peerName{};
                        if (!::restoration::account_store::presentation_by_account(
                            match.rows[i].account, platform, peerName)) { complete = false; break; }
                    }
#endif
                    if (!platform) { complete = false; break; }
                    player.awaitingClientSync = spawn_sync::awaiting_client_sync(platform);
                }
                snapshot.unresolvedParticipationRoster = !complete;
                if (!complete) snapshot.participationPlayers = {};
            }
            snapshot.scoreboardPlayerCount=match.count;
            snapshot.scoreboardTeamCount=rumble?12:2;
            for(std::size_t i=0;i<match.count;++i) {
                const auto& row=match.rows[i];
                const auto side=row.side;
                snapshot.scoreboardPlayers[i]={row.player,side,row.score,row.kills,row.deaths,
                    state::activity::crucible::native_score_headline(match,i)};
                auto& teamStats=snapshot.scoreboardTeams[side];
                teamStats.score+=row.score;teamStats.kills+=row.kills;teamStats.deaths+=row.deaths;
            }
            auto& staged = session.activityRosterStaged;
            staged.crucible = true;
            staged.cruciblePhase = static_cast<std::uint8_t>(match.phase);
            staged.crucibleRevision = match.revision;
            staged.crucibleEndTick = 0;
            if(match.phase==state::activity::crucible::host::Phase::playing
                || match.phase==state::activity::crucible::host::Phase::results)
                snapshot.allowJoinInProgress=false;
            if(match.phase==state::activity::crucible::host::Phase::results) {
                const auto now=GetTickCount64();
                staged.crucibleEndTick = session.activityCrucibleEndTick
                    ? session.activityCrucibleEndTick : now;
                staged.crucibleCompletionPhase = state::activity::crucible::completion::phase(
                    now, staged.crucibleEndTick);
                snapshot.missionCompletion={{match.key.session,1},true,
                    staged.crucibleCompletionPhase};
                const auto nativeClock = state::activity::crucible::native_completion_countdown(
                    match.durationMs, match.elapsedMs, match.endedAt,
                    staged.crucibleEndTick, now, staged.crucibleCompletionPhase);
                snapshot.nativeCountdownRunning = nativeClock.running;
                snapshot.nativeCountdown = nativeClock.valid
                    && clock::from_milliseconds(nativeClock.durationMs, snapshot.nativeCountdownDurationTicks)
                    && clock::from_milliseconds(nativeClock.remainingMs, snapshot.nativeCountdownRemainingTicks)
                    && clock::from_milliseconds(nativeClock.anchorMs, snapshot.nativeCountdownAnchorTicks)
                    && clock::from_milliseconds(nativeClock.clockMs, snapshot.gameplayClockTicks);
            }
        }
    }
    if (defaults.crucibleControlPlacementTest && !snapshot.awaitClientSync
        && !session.activityJoinedForeignSession) {
        namespace crucible = state::activity::crucible;
        const auto* map = crucible::find_native_map(name);
        const auto* mode = crucible::find_native_mode(selection.previousActivityIndex);
        if (map && map->scenarioTag == layout.tag && mode
            && mode->symbolicName == "pvp_control") {
            crucible::PlacementFrame frame{};
            frame.generation = 1;
            frame.enabled = true;
            for (std::size_t index = 0; index < frame.zoneSlots.size(); ++index) {
                crucible::ObjectiveBinding binding{};
                if (!crucible::find_objective_binding(*map,
                        crucible::ObjectiveRole::captureZone, index, binding)
                    || (index != 0 && binding.registryKey != frame.registryKey)) {
                    frame.enabled = false;
                    break;
                }
                frame.registryKey = binding.registryKey;
                frame.zoneSlots[index] = binding.slotIndex;
            }
            snapshot.cruciblePlacements = frame;
        }
    }
    snapshot.keyOnEveryParticipationSlot = defaults.rosterKeyOnAllSlots;
    // the participation record's +0 latches only when the region index is known
    snapshot.region = static_cast<std::uint32_t>(region.index);
    snapshot.hasRegion = true;
    // the spawn override always names the destination's own arrival, never the player's position
    snapshot.spawnSliceSet = region.arrival;
    snapshot.spawnSetHash =
        state::activity::destination::attachable_spawn_set_hash(selection, fallback.spawnSetHash);
    snapshot.hasSpawnOverride =
        snapshot.spawnSetHash != 0 && snapshot.spawnSetHash != message::kAbsentSpawnSetHash;
    if (missionRoster) {
        static_cast<void>(publish_mission(session, selection, region.index, snapshot));
    }
    snapshot.stateSequence = next_state_sequence(
        session, fold_groups(snapshot.roster), burst, snapshot.awaitClientSync);
    session.activityMissionDirectorActive = false;
    return RosterOutcome::published;
}

} // namespace dawn::server::bap::encrypted::push::activity
