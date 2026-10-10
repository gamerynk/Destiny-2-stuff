#include "activity_keepalive_push.h"

#include <Windows.h>

#include <array>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../core/runtime/runtime_policy.h"
#include "../../../../../state/activity/definition.h"
#include "../../../../../state/activity/shared/social_admission.h"
#include "../../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../../state/activity/mission_host/mission_host.h"
#include "../../../../../state/activity/crucible/match_runtime.h"
#include "../../../../../state/activity/crucible/completion_timing.h"
#include "../../../../../state/activity/strike_pact/runtime.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../../../gameplay/gameplay_advertisement.h"
#include "../../../../gameplay/group/group_host_sessions.h"
#include "../../activity_message/definition.h"
#include "activity_arrival.h"
#include "activity_global_state_push.h"
#include "activity_membership_publication_policy.h"
#include "activity_membership_push.h"
#include "activity_message_push.h"
#include "activity_roster_push.h"
#include "activity_spawn_sync.h"
#include "internal.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

// the client tears the session down after 20.5 s of server silence; 5 s leaves 3 writes of margin
constexpr std::uint64_t kKeepaliveIntervalMs = 5'000;
constexpr std::uint64_t kRosterBurstIntervalMs = 1'000;
constexpr std::uint64_t kMembershipFollowupDelayMs = 1;
constexpr std::int32_t kNoBubble = -1;
constexpr std::uint32_t kCurrentRevision = 0;
constexpr std::uint64_t kMissionPublishIntervalMs = 500;

[[nodiscard]] bool mission_due(Session& session, std::uint64_t now) noexcept {
    if (!core::runtime::policy::kActivityBackendReplication || session.activitySessionId == 0
        || !session.activityPatchEpochSeen || session.activityJoinedForeignSession
        || state::activity::mission_host::session() != session.activitySessionId) {
        return false;
    }
    const auto publication = state::activity::strike_pact::publication();
    if (publication.run == 0) {
        return false;
    }
    const bool changed = publication.run != session.activityMissionRun
                         || publication.revision != session.activityMissionRevision;
    // a changed frame goes out at once; an unchanged one only ticks timers, since a 100 ms cadence re-sent the whole roster and stalled the link
    if (!changed && (!publication.enabled || now < session.activityMissionDueTick)) {
        return false;
    }
    session.activityMissionDueTick = now + kMissionPublishIntervalMs;
    return true;
}

[[nodiscard]] bool publish_frame(Session& session,
                                 Scratch& scratch,
                                 std::span<std::byte> response,
                                 std::size_t& written,
                                 std::size_t framedSize,
                                 const std::array<std::byte, state::kBapNonceSize>& nextSendNonce,
                                 bool published) noexcept {
    if (!published || framedSize == 0 || framedSize > response.size()) {
        discard_staged_roster(session);
        return false;
    }
    for (std::size_t index = 0; index < framedSize; ++index) {
        response[index] = scratch.framed[index];
    }
    written = framedSize;
    session.sendNonce = nextSendNonce;
    // settle only here: the grant and state byte may move only on a delivered frame
    commit_staged_roster(session);
    return true;
}

} // namespace

bool consume_activity_keepalive(Session& session,
                                Scratch& scratch,
                                std::span<std::byte> response,
                                std::size_t& written,
                                bool& touchesScratch) noexcept {
    written = 0;
    const std::uint64_t now = GetTickCount64();
    const bool burstDue = core::runtime::policy::kActivityBackendReplication
                          && !session.activityJoinedForeignSession
                          && now < session.activityTransitionUntilTick
                          && now >= session.activityRosterDueTick;
    const bool keepaliveDue = now >= session.activityKeepaliveDueTick;
    const std::int32_t reportedRegion =
        session.activitySessionId == 0
            ? -1
            : state::activity::membership::reported_region(session.activitySessionId);
    const bool regionChanged = core::runtime::policy::kActivityBackendReplication
                               && !session.activityJoinedForeignSession && reportedRegion >= 0
                               && reportedRegion != session.activityAdvertisedRegion;
    // a ws-701 edge changes the type-13 auth body without changing its group set; publish that revision now or the old hold may stay
    const bool spawnSyncChanged =
        core::runtime::policy::kActivityBackendReplication && session.activityPatchEpochSeen
        && spawn_sync::revision(session.credentials.platformId)
               != session.activityRosterSpawnSyncRevision;
    if (session.activitySessionId == 0) {
        return false;
    }
    const bool missionDue = mission_due(session, now);
    bool crucibleDue=!session.activityJoinedForeignSession && session.activityCrucibleEndTick
        && session.activityCruciblePhase==static_cast<std::uint8_t>(state::activity::crucible::host::Phase::results)
        && state::activity::crucible::completion::phase(now,session.activityCrucibleEndTick)
            !=session.activityCrucibleCompletionPhase;
    if(!session.activityJoinedForeignSession && now>=session.activityCrucibleCheckTick) {
        session.activityCrucibleCheckTick=now+500;
        state::activity::crucible::host::View match{};std::uint64_t account{};
        (void)state::capture_account_runtime(account);
        crucibleDue=crucibleDue || (state::activity::crucible::runtime::for_account(account,match)
            && match.key.session==session.activitySessionId
            && (static_cast<std::uint8_t>(match.phase)!=session.activityCruciblePhase
                || match.revision!=session.activityCrucibleRevision));
    }
    if (!burstDue && !keepaliveDue && !regionChanged && !spawnSyncChanged && !missionDue && !crucibleDue) {
        return false;
    }
    touchesScratch = true;

    auto nextSendNonce = session.sendNonce;
    std::size_t framedSize = 0;
    const auto& key = session.credentials.bap.sessionKey;
    bool published = false;
    // held until the frame reaches the caller; encoding alone doesn't spend the region trigger
    std::int32_t stagedAdvertisedRegion = -1;
    std::int32_t stagedBaselineRegion = -1;
    // when both timers fire together the roster goes last, because the type-13 key binds to the player message 12 creates
    if (!keepaliveDue && !regionChanged) {
        session.activityRosterDueTick = now + kRosterBurstIntervalMs;
        published = append_roster_notification(
            session, scratch, key, nextSendNonce, scratch.framed, framedSize, burstDue);
        return publish_frame(
            session, scratch, response, written, framedSize, nextSendNonce, published);
    }
    session.activityKeepaliveDueTick = now + kKeepaliveIntervalMs;
    if (session.activityJoinedForeignSession) {
        // derived PUBLIC TARGET: membership must come from this session's own refresh; mirroring the private snapshot reports somebody else's region, suppressing it strands the link at MEM-0
        const std::uint64_t sourceSessionId =
            server::gameplay::group::source_activity_session_for_host(
                session.activitySessionId);
        const std::int32_t sourceRegion =
            state::activity::membership::reported_region(sourceSessionId);
        const std::int32_t targetRegion =
            server::gameplay::group::region_for_host_session(session.activitySessionId);
        const std::int32_t publicStateRegion =
            state::activity::membership::reported_region(session.activitySessionId);
        const bool assignedRegionReady =
            targetRegion >= 0 && publicStateRegion == targetRegion;
        const bool rosterReady =
            session.activityContract.ready_for_public_target_roster(assignedRegionReady);
        if (!rosterReady) {
            std::array<char, core::log::kLineCapacity> line{};
            const int count = std::snprintf(
                line.data(),
                line.size(),
                "ev=activity stage=public_target result=waiting_public_setup session=0x%llX "
                "source=0x%llX source_region=%d target_region=%d state_region=%d "
                "epoch=%u refresh=%u membership=0 roster=0 "
                "identity=%u authoritative=%u membership_published=%u roster_published=%u",
                static_cast<unsigned long long>(session.activitySessionId),
                static_cast<unsigned long long>(sourceSessionId),
                sourceRegion,
                targetRegion,
                publicStateRegion,
                static_cast<unsigned>(session.activityContract.patchEpoch),
                static_cast<unsigned>(session.activityContract.refresh),
                static_cast<unsigned>(session.activityContract.identity),
                static_cast<unsigned>(session.activityContract.authoritative),
                static_cast<unsigned>(session.activityForeignMembershipPublished),
                static_cast<unsigned>(session.activityForeignRosterPublished));
            if (count > 0) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::info,
                                 {line.data(), static_cast<std::size_t>(count)});
            }
            return false;
        }
        // identity/authoritative are optional on this path (one live run omitted both); latch a short post-setup window so membership can't race setup but still lands before normal_z_leg
        if (session.activityPublicMembershipNotBeforeTick == 0) {
            session.activityPublicMembershipNotBeforeTick =
                public_membership_settle_deadline(now);
        }
        const bool membershipReady = public_membership_settled(
            now, session.activityPublicMembershipNotBeforeTick);
        if (!membershipReady) {
            session.activityKeepaliveDueTick =
                session.activityPublicMembershipNotBeforeTick;
        }
        published = append_global_state_notification(
            scratch, session.activitySessionId, key, nextSendNonce, scratch.framed, framedSize);
        state::activity::membership::PendingMutation refresh{};
        const bool hasMembership =
            state::activity::membership::prepare_refresh(
                session.activitySessionId, kCurrentRevision, kNoBubble, refresh)
            && refresh.hasSnapshot;
        const EffectiveRegion region = effective_region(session.activitySessionId);
        const MembershipPublicationPhase phase = membership_publication_phase(
            hasMembership,
            region.index,
            session.activityMembershipBaselineRegion,
            session.activityAdvertisedRegion);
        const bool stagesBaseline = membershipReady && hasMembership
                                    && phase == MembershipPublicationPhase::baseline;
        const bool membershipAcknowledged =
            state::activity::membership::acknowledged(session.activitySessionId);
        const bool wantsMembership = public_membership_should_publish(
            membershipReady, hasMembership, phase,
            session.activityForeignMembershipPublished, membershipAcknowledged);
        const bool includesAdvertisement = phase != MembershipPublicationPhase::baseline;
        const server::gameplay::AdvertisementState advertisement =
            wantsMembership && includesAdvertisement
                ? server::gameplay::advertisement_state(session.activitySessionId, region.index)
                : server::gameplay::AdvertisementState::absent;
        bool appendedMembership = false;
        if (stagesBaseline) {
            stagedBaselineRegion = region.index;
        }
        if (wantsMembership
            && advertisement != server::gameplay::AdvertisementState::pending) {
            activity_message::ActivityPlan plan{};
            plan.sessionId = session.activitySessionId;
            plan.membershipMutation = refresh;
            appendedMembership = append_membership_notification(
                scratch,
                plan,
                key,
                nextSendNonce,
                scratch.framed,
                framedSize,
                includesAdvertisement);
            published = appendedMembership || published;
            if (appendedMembership && includesAdvertisement
                && advertisement == server::gameplay::AdvertisementState::ready) {
                stagedAdvertisedRegion = region.index;
            }
            SecureZeroMemory(&plan, sizeof plan);
        }
        const std::uint32_t membershipRevision = refresh.snapshot.revision;
        SecureZeroMemory(&refresh, sizeof refresh);
        session.activityRosterDueTick = now + kRosterBurstIntervalMs;
        const bool appendedRoster = append_roster_notification(
            session, scratch, key, nextSendNonce, scratch.framed, framedSize, false);
        published = appendedRoster || published;
        const bool delivered = publish_frame(
            session, scratch, response, written, framedSize, nextSendNonce, published);
        if (delivered && stagedBaselineRegion >= 0) {
            session.activityMembershipBaselineRegion = stagedBaselineRegion;
            session.activityKeepaliveDueTick = now + kMembershipFollowupDelayMs;
        }
        if (delivered && stagedAdvertisedRegion >= 0) {
            session.activityAdvertisedRegion = stagedAdvertisedRegion;
            session.activityForeignMembershipPublished = true;
        }
        if (advertisement == server::gameplay::AdvertisementState::pending) {
            session.activityKeepaliveDueTick = now + kMembershipFollowupDelayMs;
        }
        if (delivered && appendedRoster) {
            session.activityForeignRosterPublished = true;
        }
        std::array<char, core::log::kLineCapacity> line{};
        const int count = std::snprintf(
            line.data(),
            line.size(),
            "ev=activity stage=public_target result=%s session=0x%llX source=0x%llX "
            "source_region=%d target_region=%d state_region=%d region_source=%s "
            "public_ready=1 membership=%u roster=%u "
            "membership_published=%u roster_published=%u phase=%u baseline_region=%d "
            "advertised_region=%d membership_ready=%u membership_not_before=%llu advert=%u "
            "revision=%u acknowledged=%u",
            delivered ? "published" : "failed",
            static_cast<unsigned long long>(session.activitySessionId),
            static_cast<unsigned long long>(sourceSessionId),
            sourceRegion,
            targetRegion,
            publicStateRegion,
            session.activityContract.region ? "client" : "assigned_target",
            static_cast<unsigned>(appendedMembership),
            static_cast<unsigned>(appendedRoster),
            static_cast<unsigned>(session.activityForeignMembershipPublished),
            static_cast<unsigned>(session.activityForeignRosterPublished),
            static_cast<unsigned>(phase),
            static_cast<int>(session.activityMembershipBaselineRegion),
            static_cast<int>(session.activityAdvertisedRegion),
            static_cast<unsigned>(membershipReady),
            static_cast<unsigned long long>(
                session.activityPublicMembershipNotBeforeTick),
            static_cast<unsigned>(advertisement),
            membershipRevision,
            static_cast<unsigned>(membershipAcknowledged));
        if (count > 0) {
            core::log::write(core::log::Channel::server,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(count)});
        }
        return delivered;
    }
    published = append_global_state_notification(
        scratch, session.activitySessionId, key, nextSendNonce, scratch.framed, framedSize);
    if constexpr (!core::runtime::policy::kActivityBackendReplication) {
        session.activityRosterDueTick = 0;
        return publish_frame(
            session, scratch, response, written, framedSize, nextSendNonce, published);
    } else {

#pragma warning(push)
#pragma warning(disable : 4127)
    if (regionChanged
        && server::gameplay::advertisement_state(session.activitySessionId, reportedRegion)
               == server::gameplay::AdvertisementState::ready
        && state::activity::membership::acknowledged(session.activitySessionId)
        && state::activity::membership::republish(session.activitySessionId)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=gameplay stage=membership result=republished reason=region");
    }
#pragma warning(pop)
    state::activity::membership::PendingMutation refresh{};
    bool hasMembership = state::activity::membership::prepare_refresh(
                             session.activitySessionId, kCurrentRevision, kNoBubble, refresh)
                         && refresh.hasSnapshot;
    if (!hasMembership
        && seed_identity(
            session.activitySessionId, session.activityMemberKey, session.activityCharacterSoid)) {
        SecureZeroMemory(&refresh, sizeof refresh);
        hasMembership = state::activity::membership::prepare_refresh(
                            session.activitySessionId, kCurrentRevision, kNoBubble, refresh)
                        && refresh.hasSnapshot;
    }
    if (hasMembership && refresh.snapshot.transitionToken == 0
        && seed_transition_token(session.activitySessionId)) {
        SecureZeroMemory(&refresh, sizeof refresh);
        hasMembership = state::activity::membership::prepare_refresh(
                            session.activitySessionId, kCurrentRevision, kNoBubble, refresh)
                        && refresh.hasSnapshot;
    }
    const EffectiveRegion region = effective_region(session.activitySessionId);
    const MembershipPublicationPhase phase = membership_publication_phase(
        hasMembership,
        region.index,
        session.activityMembershipBaselineRegion,
        session.activityAdvertisedRegion);
    // retail accepts one membership body per revision; the baseline turn sends none and the advertisement turn spends the revision once with the Citizen descriptor
    const bool stagesBaseline =
        hasMembership && phase == MembershipPublicationPhase::baseline;
    const bool publishesMembership =
        hasMembership && membership_phase_sends_snapshot(phase)
        && (phase == MembershipPublicationPhase::advertisement || regionChanged
            || !state::activity::membership::acknowledged(session.activitySessionId));
    const bool includesAdvertisement = phase != MembershipPublicationPhase::baseline;
    const server::gameplay::AdvertisementState advertisement =
        publishesMembership && includesAdvertisement
            ? server::gameplay::advertisement_state(session.activitySessionId, region.index)
                            : server::gameplay::AdvertisementState::absent;
    if (stagesBaseline) {
        stagedBaselineRegion = region.index;
    }
    if (publishesMembership && advertisement == server::gameplay::AdvertisementState::pending) {
        // pushing while the advertisement is still allocating spends the revision on a region record with no join descriptor; the allocation lands next slice, so hold one poll
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         "ev=gameplay stage=membership result=held reason=no_host_session");
    } else if (publishesMembership) {
        activity_message::ActivityPlan plan{};
        plan.sessionId = session.activitySessionId;
        plan.membershipMutation = refresh;
        const bool sent = append_membership_notification(
            scratch,
            plan,
            key,
            nextSendNonce,
            scratch.framed,
            framedSize,
            includesAdvertisement);
        if (primary_region_publication_complete(sent, includesAdvertisement,
            advertisement == server::gameplay::AdvertisementState::ready,
            state::activity::shared::social_session(session.activitySessionId),
            advertisement == server::gameplay::AdvertisementState::absent)) {
            stagedAdvertisedRegion = region.index;
        }
        published = sent || published;
        SecureZeroMemory(&plan, sizeof plan);
    }
    const int reportedSpawnState = static_cast<int>(refresh.snapshot.spawn.state);
    const int reportedTeleportState = static_cast<int>(refresh.snapshot.teleport.state);
    const std::int32_t reportedTeleportSlice = refresh.snapshot.teleport.sliceSetIndex;
    const std::uint8_t reportedToken = refresh.snapshot.transitionToken;
    const std::uint32_t reportedRevision = refresh.snapshot.revision;
    SecureZeroMemory(&refresh, sizeof refresh);
    session.activityRosterDueTick = now + kRosterBurstIntervalMs;
    published = append_roster_notification(
                    session, scratch, key, nextSendNonce, scratch.framed, framedSize, false)
                || published;

    std::array<char, core::log::kLineCapacity> line{};
    const int count =
        std::snprintf(line.data(),
                      line.size(),
                      "ev=activity stage=keepalive result=%s bytes=%zu membership=%u key=0x%llX "
                      "spawn_state=%d teleport_state=%d teleport_slice=%d token=%u revision=%u "
                      "phase=%u baseline_region=%d advertised_region=%d advert=%u",
                      published ? "ok" : "fail",
                      framedSize,
                      hasMembership ? 1U : 0U,
                      static_cast<unsigned long long>(session.activityMemberKey),
                      reportedSpawnState,
                      reportedTeleportState,
                      reportedTeleportSlice,
                      static_cast<unsigned>(reportedToken),
                      reportedRevision,
                      static_cast<unsigned>(phase),
                      static_cast<int>(session.activityMembershipBaselineRegion),
                      static_cast<int>(session.activityAdvertisedRegion),
                      static_cast<unsigned>(advertisement));
    if (count > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::debug,
                         {line.data(), static_cast<std::size_t>(count)});
    }
    const bool delivered =
        publish_frame(session, scratch, response, written, framedSize, nextSendNonce, published);
    if (delivered && stagedBaselineRegion >= 0) {
        session.activityMembershipBaselineRegion = stagedBaselineRegion;
        session.activityKeepaliveDueTick = now + kMembershipFollowupDelayMs;
    }
    if (delivered && stagedAdvertisedRegion >= 0) {
        session.activityAdvertisedRegion = stagedAdvertisedRegion;
    }
    if (advertisement == server::gameplay::AdvertisementState::pending) {
        session.activityKeepaliveDueTick = now + kMembershipFollowupDelayMs;
    }
        return delivered;
    }
}

} // namespace dawn::server::bap::encrypted::push::activity
