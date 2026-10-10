#pragma once

#include <array>
#include "../../middleware/datagen/family4/character/acquisition_feedback.h"
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../client/network/consumer.h"
#include "../../middleware/bap/activity_message/activity_patch_epoch_parser.h"
#include "../../middleware/bap/activity_message/sensor_auth_update.h"
#include "../../middleware/bap/frame.h"
#include "../../state/activity/bubble_authority/definition.h"
#include "../../state/activity/entity_slots/definition.h"
#include "../../state/build_data/scenarios/definition.h"
#include "../../state/runtime/state.h"
#include "../../state/session/session_runtime.h"
#include "encrypted/queuez/definition.h"
#include "social_roster_presence_policy.h"

namespace dawn::server::bap {

inline constexpr std::size_t kSessionCount = client::network::kBapConnectionCount;
static_assert(state::matchmaking::kContextCapacity >= kSessionCount,
              "Every authenticated BAP channel needs its own matchmaking context");

[[nodiscard]] std::int16_t roster_activity_index_locked(std::uint64_t accountSoid) noexcept;

inline constexpr std::size_t kRosterSubBlockKeyCapacity =
    middleware::bap::activity_message::sensor_auth_update::kBubbleKeyCapacity;
static_assert(kRosterSubBlockKeyCapacity
              >= state::build_data::scenarios::kDestinationBubbleGroupCapacity);

struct Scratch {
    std::array<std::byte, client::network::kBapFrameCapacity> plaintext{};
    std::array<std::byte, client::network::kBapFrameCapacity> responseBody{};
    std::array<std::byte, client::network::kBapFrameCapacity> responsePayload{};
    std::array<std::byte, client::network::kBapFrameCapacity> sealed{};
    std::array<std::byte, client::network::kBapFrameCapacity> framed{};
    std::array<state::build_data::scenarios::RosterGroup,
               middleware::bap::activity_message::sensor_auth_update::kGroupCapacity>
        rosterGroups{};
    std::array<middleware::bap::activity_message::sensor_auth_update::BubbleSubBlock,
               state::build_data::scenarios::kBubbleCapacity>
        rosterSubBlocks{};
    std::array<std::array<std::uint32_t, kRosterSubBlockKeyCapacity>,
               state::build_data::scenarios::kBubbleCapacity>
        rosterSubBlockKeys{};
};

struct RosterPublication {
    state::activity::bubble_authority::Grant grant{};
    std::uint32_t priorGroups{};
    std::uint64_t priorSpawnSyncRevision{};
    std::uint8_t priorSends{};
    std::uint8_t priorState{};
    std::uint64_t missionRun{};
    std::uint32_t missionRevision{};
    bool mission{};
    std::uint64_t crucibleEndTick{};
    std::uint8_t cruciblePhase{};
    std::uint8_t crucibleCompletionPhase{};
    std::uint32_t crucibleRevision{};
    bool crucible{};
    bool priorMissionDirectorActive{};
    bool priorClientSyncKnown{};
    bool priorAwaitClientSync{};
    bool hasGrant{};
    bool staged{};
};

struct ActivityClientContract {
    std::int32_t reportedRegion{-1};
    std::uint32_t lastMessageType{};
    std::uint16_t lastReportedMask{};
    std::uint8_t reportedTransitionToken{};
    bool allocated{};
    bool joined{};
    bool identity{};
    bool patchEpoch{};
    bool authoritative{};
    bool region{};
    bool transitionToken{};
    bool refresh{};
    bool acknowledgement{};

    [[nodiscard]] bool ready_for_publication(bool inheritedRegion = false) const noexcept {
        return allocated && joined && patchEpoch && authoritative && (region || inheritedRegion)
               && transitionToken && refresh;
    }

    [[nodiscard]] bool
    ready_for_public_target_roster(bool assignedRegion = false) const noexcept {
        return allocated && joined && patchEpoch && refresh && (region || assignedRegion);
    }

};

struct Session {
    std::uint64_t connectionIncarnation{},authenticatedOrdinal{};
    std::uint32_t id{};
    bool authenticated{};
    std::uint64_t openedAtTick{};
    std::uint64_t lastFrameAtTick{};
    state::session::Credentials credentials{};
    state::AccountRuntimeContext accountRuntime{};
    std::array<std::byte, state::kBapNonceSize> sendNonce{};
    std::array<std::byte, state::kBapNonceSize> receiveNonce{};
    state::matchmaking::ContextHandle matchmakingContext{};
    std::uint64_t activitySessionId{};
    std::uint64_t activityRetirementJoinRevision{};
    std::uint64_t activityPublishedRetirementSerial{};
    ActivityClientContract activityContract{};
    std::uint64_t activityKeepaliveDueTick{};
    std::uint64_t activityMemberKey{};
    // roster participation key must be the character the client signed in on; it binds its player by that
    std::uint64_t activityCharacterSoid{};
    std::uint64_t activityRosterDueTick{};
    // client is loading until this tick; only a join or a transition-token change opens it
    std::uint64_t activityTransitionUntilTick{};
    middleware::bap::activity_message::patch_epoch::PatchEpoch activityPatchEpoch{};
    std::uint32_t activityRosterGroups{};
    std::uint8_t activityRosterSends{};
    std::uint8_t activityRosterLoggedTeam{};
    std::uint8_t activityRosterState{};
    std::uint64_t activityRosterSpawnSyncRevision{};
    bool activityRosterClientSyncKnown{};
    bool activityRosterAwaitClientSync{};
    std::uint16_t activityDirectorSends{};
    bool activityMissionDirectorActive{};
    bool activityPatchEpochSeen{};
    // each fireteam member gets its own push when the shared frame changes; a new run resets the revision
    std::uint64_t activityMissionRun{};
    std::uint32_t activityMissionRevision{};
    std::uint64_t activityMissionDueTick{};
    std::uint64_t activityCrucibleCheckTick{}, activityCrucibleEndTick{};
    std::uint8_t activityCruciblePhase{};
    std::uint8_t activityCrucibleCompletionPhase{};
    std::uint32_t activityCrucibleRevision{};
    std::uint32_t activityMissionRosterLogged{};
    // a release after a held spawn means the member arrived in the world; a never-held spawn proves nothing
    std::uint64_t activityMissionHoldSession{};
    bool activityJoinedForeignSession{};
    bool activityForeignMembershipPublished{};
    bool activityForeignRosterPublished{};
    std::uint64_t activityPublicMembershipNotBeforeTick{};
    // -1 until the first baseline; every region move needs a new baseline before its citizen descriptor
    std::int32_t activityMembershipBaselineRegion{-1};
    // moves only on a delivered frame with a descriptor; otherwise the region-change trigger stays armed
    std::int32_t activityAdvertisedRegion{-1};
    // the push runs every second, so log a refusal only when the reason changes
    std::uint8_t activityRosterReason{};
    RosterPublication activityRosterStaged{};
    encrypted::queuez::SessionState queuez{};
    remote_appearance::Tracker foreignAppearance{};
    std::uint64_t family4RepushDueTick{};
    std::uint64_t family4RepushRoot{};
    bool family4RepushArmed{};
    std::uint64_t bannerRepushDueTick{};
    std::uint64_t bannerRepushRoot{};
    bool bannerRepushArmed{};
    std::uint64_t socialRosterRepushDueTick{};
    std::uint64_t socialRosterRepushRoot{};
    bool socialRosterRepushArmed{};
    social_roster_presence::Tracker socialRosterPresence{};
    social_roster_presence::ArrivalLatch socialRosterArrival{};
    std::uint64_t joinRepushDueTick{};
    std::uint64_t joinRepushDeadlineTick{};
    std::uint64_t joinRepushRoot{};
    bool joinRepushArmed{};
    std::uint64_t accountGeneration{};
    std::uint64_t accountResyncGeneration{};
    bool accountMutationPublished{};
    bool accountResyncArmed{};
    std::array<middleware::datagen::family4::character::AcquisitionNotice, 1024> accountAcquisitions{};
    std::size_t accountAcquisitionCount{};
    // roster create/delete: the select move carries no new item records or slot count, so the origin needs a full resync
    bool accountStructuralChange{};
};

namespace plaintext {

[[nodiscard]] bool consume(Session& session,
                           Scratch& scratch,
                           const middleware::bap::OuterFrame& outer,
                           std::span<std::byte> response,
                           std::size_t& written) noexcept;

} // namespace plaintext

namespace encrypted {

[[nodiscard]] bool consume(Session& session,
                           Scratch& scratch,
                           const middleware::bap::OuterFrame& outer,
                           std::span<std::byte> response,
                           std::size_t& written) noexcept;

[[nodiscard]] bool consume_deferred(Session& session,
                                    Scratch& scratch,
                                    std::span<std::byte> response,
                                    std::size_t& written,
                                    bool& touchesScratch) noexcept;

} // namespace encrypted

} // namespace dawn::server::bap
