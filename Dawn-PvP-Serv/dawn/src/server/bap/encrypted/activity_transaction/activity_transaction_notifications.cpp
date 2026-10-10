#include "activity_transaction_notifications.h"

#include "../../../../core/logging/log.h"
#include "../../../../core/runtime/runtime_policy.h"
#include "../../../gameplay/gameplay_advertisement.h"
#include "../../../gameplay/group/group_host_sessions.h"
#include "../push/activity/activity_arrival.h"
#include "../push/activity/activity_global_state_push.h"
#include "../push/activity/activity_membership_publication_policy.h"
#include "../push/activity/activity_membership_push.h"
#include "../push/activity/activity_message_push.h"
#include "../push/activity/activity_roster_push.h"
#include "../../../../middleware/bap/activity_message/activity_player_assignment.h"
#include "../../../../state/activity/destination/activity_destination_snapshot.h"

namespace dawn::server::bap::encrypted::activity_transaction {
namespace {

const core::protocol::ActivityPlayerTeam* prepared_team(const activity_message::ActivityPlan& activity) noexcept {
    return activity.membershipMutation.hasSnapshot
        ? &activity.membershipMutation.snapshot.peerConnection.playerTeam : nullptr;
}
bool changes_published_team(const activity_message::ActivityPlan& activity) noexcept {
    const auto* pending=prepared_team(activity);
    if (!pending) return false;
    state::activity::destination::DestinationSelection selection{};
    if (!state::activity::destination::snapshot(activity.sessionId,selection)) return false;
    namespace assignments=middleware::bap::activity_message::player_assignment;
    const bool competitive=assignments::competitive_destination(std::string_view(
        reinterpret_cast<const char*>(selection.packageName.data()),selection.packageNameLength));
    return assignments::resolve(competitive,*pending)!=assignments::resolve(competitive,
        state::activity::membership::player_team(activity.sessionId));
}

[[nodiscard]] bool assigned_public_region_ready(const Session& session) noexcept {
    if (!session.activityJoinedForeignSession || session.activitySessionId == 0) {
        return false;
    }
    const std::int32_t assigned =
        gameplay::group::region_for_host_session(session.activitySessionId);
    return assigned >= 0
           && state::activity::membership::reported_region(session.activitySessionId) == assigned;
}

[[nodiscard]] push::activity::MembershipPublicationPhase publication_phase(
    const Session& session,
    const activity_message::ActivityPlan& activity) noexcept {
    const auto region =
        push::activity::planned_region(activity.membershipMutation, activity.sessionId);
    return push::activity::membership_publication_phase(
        activity.membershipMutation.hasSnapshot,
        region.index,
        session.activityMembershipBaselineRegion,
        session.activityAdvertisedRegion);
}

// defer region settlement to the keepalive publisher, which can prove a frame reached the client without spending the revision first
[[nodiscard]] bool defer_to_keepalive(const Session& session,
                                      const activity_message::ActivityPlan& activity) noexcept {
    const auto phase = publication_phase(session, activity);
    const bool deferred = phase == push::activity::MembershipPublicationPhase::baseline
                          || phase == push::activity::MembershipPublicationPhase::advertisement;
    if (deferred) {
        core::log::write(
            core::log::Channel::server,
            core::log::Level::debug,
            phase == push::activity::MembershipPublicationPhase::baseline
                ? "ev=gameplay stage=membership result=deferred reason=region_baseline"
                : "ev=gameplay stage=membership result=deferred reason=region_advertisement");
    }
    return deferred;
}

// same order as the keepalive: global state, membership, roster; the roster's participation key binds to the player membership publishes
[[nodiscard]] bool stage_refresh(Session& session,
                                 Scratch& scratch,
                                 const activity_message::ActivityPlan& activity,
                                 std::span<const std::byte, state::kAesKeySize> key,
                                 std::array<std::byte, state::kBapNonceSize>& nonce,
                                 std::span<std::byte> response,
                                 std::size_t& written) noexcept {
    // the join reply already carries type-1 state; replaying it while the client promotes that reply makes retail run activity_host_changed twice and clear the host
    // commit the refresh but defer its answer until the public client supplies its patch epoch
    if (session.activityJoinedForeignSession
        && !session.activityContract.ready_for_public_target_roster(
            assigned_public_region_ready(session))) {
        return true;
    }
    bool staged = push::activity::append_global_state_notification(
        scratch, activity.sessionId, key, nonce, response, written);
    if constexpr (!core::runtime::policy::kActivityBackendReplication) {
        return staged;
    }
    if (activity.membershipMutation.hasSnapshot && !defer_to_keepalive(session, activity)) {
        staged = push::activity::append_membership_notification(
                     scratch, activity, key, nonce, response, written)
                 || staged;
    }
    return push::activity::append_roster_notification(
               session, scratch, key, nonce, response, written, false,prepared_team(activity))
           || staged;
}

[[nodiscard]] bool stage_authoritative(Session& session,
                                       Scratch& scratch,
                                       const activity_message::ActivityPlan& activity,
                                       std::span<const std::byte, state::kAesKeySize> key,
                                       std::array<std::byte, state::kBapNonceSize>& nonce,
                                       std::span<std::byte> response,
                                       std::size_t& written) noexcept {
    if constexpr (!core::runtime::policy::kActivityBackendReplication) {
        return true;
    }
    bool staged = false;
    bool deferred = false;
    if (activity.membershipMutation.hasSnapshot) {
        deferred = defer_to_keepalive(session, activity);
        if (!deferred) {
            staged = push::activity::append_membership_notification(
                scratch, activity, key, nonce, response, written);
        }
    }
    if (activity.regionMoved || (!deferred && changes_published_team(activity))) {
        // the region delta and its roster are one native response; omitting it strands the initial PUB408 load before PUB24 normal-z
        staged = push::activity::append_roster_notification(
                     session, scratch, key, nonce, response, written, false,prepared_team(activity))
                 || staged;
    }
    return staged || deferred;
}

} // namespace

bool stage_notifications(Session& session,
                         Scratch& scratch,
                         const activity_message::ActivityPlan& activity,
                         std::span<const std::byte, state::kAesKeySize> key,
                         std::array<std::byte, state::kBapNonceSize>& nonce,
                         std::span<std::byte> response,
                         std::size_t& written) noexcept {
    if (activity.delivery == activity_message::Delivery::joinNotifications) {
        return push::activity::append_join_notifications(
            scratch, activity, key, nonce, response, written);
    }
    if (activity.delivery == activity_message::Delivery::entitySlotNotification) {
        return push::activity::append_entity_slot_notification(scratch,
                                                               activity.sessionId,
                                                               activity.entitySlotMutation.mask,
                                                               key,
                                                               nonce,
                                                               response,
                                                               written);
    }
    if (activity.delivery == activity_message::Delivery::membershipNotification) {
        if constexpr (!core::runtime::policy::kActivityBackendReplication) {
            return true;
        }
        if (defer_to_keepalive(session, activity)) {
            return true;
        }
        return push::activity::append_membership_notification(
            scratch, activity, key, nonce, response, written);
    }
    if (activity.delivery == activity_message::Delivery::refreshNotifications) {
        return stage_refresh(session, scratch, activity, key, nonce, response, written);
    }
    if (activity.delivery == activity_message::Delivery::authoritativeNotifications) {
        return stage_authoritative(session, scratch, activity, key, nonce, response, written);
    }
    return activity.delivery == activity_message::Delivery::none;
}

} // namespace dawn::server::bap::encrypted::activity_transaction
