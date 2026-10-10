#include "activity_roster_push.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "../../../../../core/runtime/runtime_policy.h"
#include "../../../../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../../../../middleware/bap/activity_message/native/activity_clock_authority.h"
#include "../../../../../middleware/secure_channel/runtime.h"
#include "../../../../../state/activity/bubble_authority/runtime.h"
#include "../../../../../state/activity/mission_host/mission_host.h"
#include "activity_notification_frame.h"
#include "internal.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

namespace message = middleware::bap::activity_message::sensor_auth_update;

constexpr std::int32_t kNoGrant = -1;
constexpr std::size_t kDestinationCapacity = 40;

} // namespace

bool append_roster_notification(Session& session,
                                Scratch& scratch,
                                std::span<const std::byte, state::kAesKeySize> key,
                                std::array<std::byte, state::kBapNonceSize>& nonce,
                                std::span<std::byte> response,
                                std::size_t& written,
                                bool burst,
                                const core::protocol::ActivityPlayerTeam* preparedTeam) noexcept {
    if constexpr (!core::runtime::policy::kActivityBackendReplication) {
        return false;
    }
    if (written > response.size()) {
        return false;
    }
    const std::uint32_t initialRosterGroups = session.activityRosterGroups;
    const std::uint64_t initialSpawnSyncRevision = session.activityRosterSpawnSyncRevision;
    const std::uint8_t initialRosterSends = session.activityRosterSends;
    const std::uint8_t initialRosterState = session.activityRosterState;
    const bool initialClientSyncKnown = session.activityRosterClientSyncKnown;
    const bool initialAwaitClientSync = session.activityRosterAwaitClientSync;
    const std::uint16_t initialDirectorSends = session.activityDirectorSends;
    const bool initialMissionDirectorActive = session.activityMissionDirectorActive;
    message::Snapshot snapshot{};
    session.activityRosterStaged.mission = false;
    session.activityRosterStaged.missionRun = 0;
    session.activityRosterStaged.missionRevision = 0;
    session.activityRosterStaged.crucible = false;
    session.activityRosterStaged.crucibleEndTick = 0;
    session.activityRosterStaged.cruciblePhase = 0;
    session.activityRosterStaged.crucibleCompletionPhase = 0;
    std::array<char, kDestinationCapacity> destination{};
    std::size_t destinationLength = 0;
    RosterOutcome outcome = RosterOutcome::noEpoch;
    if (session.activityPatchEpochSeen) {
        outcome = build_roster_snapshot(
            session, scratch, snapshot, destination, destinationLength, burst,preparedTeam);
    }
    const std::string_view name(destination.data(), destinationLength);
    if (outcome != RosterOutcome::published) {
        report_roster_push(session, snapshot, name, 0, kNoGrant, outcome);
        return false;
    }

    // pick the grant here but commit it only once the frame reaches the caller; a discarded body leaves the bubble ungranted so the next push retries
    state::activity::bubble_authority::Grant grant{};
    // the grant follows the player, not the destination: the client's region moves as it walks between bubbles
    // the wire field is unsigned; a value past the signed range goes negative and the selector rejects it
    if (state::activity::bubble_authority::select_grant(
            session.activitySessionId, static_cast<std::int32_t>(snapshot.region), grant)) {
        snapshot.hasGrant = true;
        snapshot.grant.bubble = grant.bubble;
        snapshot.grant.token = grant.token;
    }

    const std::size_t initialWritten = written;
    auto initialNonce = nonce;
    std::size_t messageSize = 0;
    bool encoded = true;
    if (snapshot.nativeCountdown) {
        // the native scenario resets its timing interval to zero; publish the clock config before the authority frame
        // both frames share the rollback below so a failed second frame can't spend a nonce
        namespace clock = middleware::bap::activity_message::native::activity_clock;
        const auto configuration = snapshot.nativeCountdownRunning
            ? clock::kRunningCrucible : clock::Configuration{false, 0.0F};
        encoded = clock::encode(configuration, scratch.responseBody, messageSize)
            && append_notification_frame(scratch, session.activitySessionId,
                clock::kMessageType, std::span(scratch.responseBody).first(messageSize),
                key, nonce, response, written);
        if (encoded) middleware::secure_channel::advance_nonce(nonce);
        SecureZeroMemory(scratch.responseBody.data(), messageSize);
        messageSize = 0;
    }
    encoded = encoded && message::encode_sensor_auth_update(snapshot, scratch.responseBody, messageSize)
                   && append_notification_frame(scratch,
                                                session.activitySessionId,
                                                message::kMessageType,
                                                std::span(scratch.responseBody).first(messageSize),
                                                key,
                                                nonce,
                                                response,
                                                written);
    if (encoded) {
        middleware::secure_channel::advance_nonce(nonce);
        session.activityRosterStaged.grant = grant;
        session.activityRosterStaged.priorGroups = initialRosterGroups;
        session.activityRosterStaged.priorSpawnSyncRevision = initialSpawnSyncRevision;
        session.activityRosterStaged.priorSends = initialRosterSends;
        session.activityRosterStaged.priorState = initialRosterState;
        session.activityRosterStaged.priorMissionDirectorActive =
            initialMissionDirectorActive;
        session.activityRosterStaged.priorClientSyncKnown = initialClientSyncKnown;
        session.activityRosterStaged.priorAwaitClientSync = initialAwaitClientSync;
        session.activityRosterStaged.hasGrant = snapshot.hasGrant;
        session.activityRosterStaged.staged = true;
    }
    report_roster_push(session,
                       snapshot,
                       name,
                       encoded ? messageSize : 0,
                       encoded && snapshot.hasGrant ? snapshot.grant.bubble : kNoGrant,
                       encoded ? RosterOutcome::published : RosterOutcome::encodeFailed);
    SecureZeroMemory(scratch.responseBody.data(), messageSize);
    if (!encoded) {
        if (written > initialWritten) {
            SecureZeroMemory(response.data() + initialWritten, written - initialWritten);
        }
        written = initialWritten;
        nonce = initialNonce;
        session.activityRosterGroups = initialRosterGroups;
        session.activityRosterSpawnSyncRevision = initialSpawnSyncRevision;
        session.activityRosterSends = initialRosterSends;
        session.activityRosterState = initialRosterState;
        session.activityRosterClientSyncKnown = initialClientSyncKnown;
        session.activityRosterAwaitClientSync = initialAwaitClientSync;
        session.activityDirectorSends = initialDirectorSends;
        session.activityMissionDirectorActive = initialMissionDirectorActive;
    }
    SecureZeroMemory(&initialNonce, sizeof initialNonce);
    return encoded;
}

void commit_staged_roster(Session& session) noexcept {
    if (!session.activityRosterStaged.staged) {
        return;
    }
    if (session.activityRosterStaged.mission) {
        session.activityMissionRun = session.activityRosterStaged.missionRun;
        session.activityMissionRevision = session.activityRosterStaged.missionRevision;
    }
    if (session.activityRosterStaged.crucible) {
        session.activityCrucibleEndTick = session.activityRosterStaged.crucibleEndTick;
        session.activityCruciblePhase = session.activityRosterStaged.cruciblePhase;
        session.activityCrucibleCompletionPhase = session.activityRosterStaged.crucibleCompletionPhase;
        session.activityCrucibleRevision = session.activityRosterStaged.crucibleRevision;
    }
    if (session.activityRosterStaged.hasGrant) {
        state::activity::bubble_authority::record_grant(session.activitySessionId,
                                                        session.activityRosterStaged.grant);
        state::activity::mission_host::note_authority(session.activitySessionId,
                                                      session.credentials.platformId);
    }
    session.socialRosterArrival.note_delivered(session.activitySessionId,
                                               !session.activityJoinedForeignSession,
                                               session.activityRosterClientSyncKnown,
                                               session.activityRosterAwaitClientSync);
    session.activityRosterStaged = {};
}

void discard_staged_roster(Session& session) noexcept {
    if (!session.activityRosterStaged.staged) {
        return;
    }
    // the client never saw this body, so its state byte must not be spent; the next push has to move it again or the roster objects aren't rebuilt
    session.activityRosterGroups = session.activityRosterStaged.priorGroups;
    session.activityRosterSpawnSyncRevision =
        session.activityRosterStaged.priorSpawnSyncRevision;
    session.activityRosterSends = session.activityRosterStaged.priorSends;
    session.activityRosterState = session.activityRosterStaged.priorState;
    session.activityRosterClientSyncKnown =
        session.activityRosterStaged.priorClientSyncKnown;
    session.activityRosterAwaitClientSync =
        session.activityRosterStaged.priorAwaitClientSync;
    session.activityMissionDirectorActive =
        session.activityRosterStaged.priorMissionDirectorActive;
    session.activityRosterStaged = {};
}

} // namespace dawn::server::bap::encrypted::push::activity
