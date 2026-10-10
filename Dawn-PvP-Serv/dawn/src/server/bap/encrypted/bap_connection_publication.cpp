#include "bap_connection_publication.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../core/runtime/runtime_policy.h"
#include "../../../state/activity/runtime.h"
#include "../../../state/runtime/runtime.h"
#include "../../gameplay/group/group_host_sessions.h"
#include "push/activity/activity_arrival.h"
#include "push/activity/activity_membership_publication_policy.h"
#include "push/activity/internal.h"
#include "push/activity/activity_spawn_sync.h"
#include "push/activity/activity_spawn_join_ack_policy.h"

namespace dawn::server::bap::encrypted {
namespace {

// the banner pair lands unsolicited and hits the same record-state race
constexpr std::uint64_t kBannerRepushDelayMs = 400;
constexpr std::uint64_t kSocialRosterRepushDelayMs = 400;
constexpr std::uint64_t kJoinRepushDelayMs = 400;
constexpr std::uint64_t kJoinRepushWindowMs = 30'000;
// the slice-set load step takes 9.2 to 14.1 s
constexpr std::uint64_t kTransitionWindowMs = 15'000;
enum ContractBit : std::uint16_t {
    kContractAllocated = 1U << 0U,
    kContractJoined = 1U << 1U,
    kContractIdentity = 1U << 2U,
    kContractPatchEpoch = 1U << 3U,
    kContractAuthoritative = 1U << 4U,
    kContractRegion = 1U << 5U,
    kContractTransitionToken = 1U << 6U,
    kContractRefresh = 1U << 7U,
    kContractAcknowledgement = 1U << 8U,
};

[[nodiscard]] bool assigned_public_region_ready(const Session& session) noexcept {
    if (!session.activityJoinedForeignSession || session.activitySessionId == 0) {
        return false;
    }
    const std::int32_t assigned =
        gameplay::group::region_for_host_session(session.activitySessionId);
    return assigned >= 0
           && state::activity::membership::reported_region(session.activitySessionId) == assigned;
}

[[nodiscard]] std::uint16_t contract_mask(const ActivityClientContract& contract) noexcept {
    const unsigned mask = (contract.allocated ? kContractAllocated : 0U)
                          | (contract.joined ? kContractJoined : 0U)
                          | (contract.identity ? kContractIdentity : 0U)
                          | (contract.patchEpoch ? kContractPatchEpoch : 0U)
                          | (contract.authoritative ? kContractAuthoritative : 0U)
                          | (contract.region ? kContractRegion : 0U)
                          | (contract.transitionToken ? kContractTransitionToken : 0U)
                          | (contract.refresh ? kContractRefresh : 0U)
                          | (contract.acknowledgement ? kContractAcknowledgement : 0U);
    return static_cast<std::uint16_t>(mask);
}

[[nodiscard]] const char* first_missing_input(const Session& session) noexcept {
    const ActivityClientContract& contract = session.activityContract;
    if (!contract.allocated) {
        return "allocation";
    }
    if (!contract.joined) {
        return "join";
    }
    if (!contract.patchEpoch) {
        return "patch_epoch";
    }
    if (session.activityJoinedForeignSession) {
        if (!contract.refresh) {
            return "refresh";
        }
        return "parent_region_external";
    }
    if (!contract.authoritative) {
        return "authoritative";
    }
    if (!contract.region) {
        return session.activityJoinedForeignSession ? "parent_region" : "region";
    }
    if (!contract.transitionToken) {
        return "transition_token";
    }
    if (!contract.refresh) {
        return "refresh";
    }
    return "none";
}

void report_contract(Session& session) noexcept {
    ActivityClientContract& contract = session.activityContract;
    const std::uint16_t mask = contract_mask(contract);
    if (mask == contract.lastReportedMask) {
        return;
    }
    contract.lastReportedMask = mask;
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=activity_backend stage=client_contract session=0x%llX mask=0x%03X "
        "allocated=%u joined=%u identity=%u epoch=%u authoritative=%u region=%u "
        "region_index=%d token=%u token_value=%u refresh=%u acknowledgement=%u "
        "last_type=%u member=0x%llX character=0x%llX epoch_value=0x%llX,0x%llX blocked=%s",
        static_cast<unsigned long long>(session.activitySessionId),
        static_cast<unsigned>(mask),
        contract.allocated ? 1U : 0U,
        contract.joined ? 1U : 0U,
        contract.identity ? 1U : 0U,
        contract.patchEpoch ? 1U : 0U,
        contract.authoritative ? 1U : 0U,
        contract.region ? 1U : 0U,
        contract.reportedRegion,
        contract.transitionToken ? 1U : 0U,
        static_cast<unsigned>(contract.reportedTransitionToken),
        contract.refresh ? 1U : 0U,
        contract.acknowledgement ? 1U : 0U,
        contract.lastMessageType,
        static_cast<unsigned long long>(session.activityMemberKey),
        static_cast<unsigned long long>(session.activityCharacterSoid),
        static_cast<unsigned long long>(session.activityPatchEpoch.first),
        static_cast<unsigned long long>(session.activityPatchEpoch.second),
        first_missing_input(session));
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

void report_spawn_sync_arm(const Session& session,
                           push::activity::spawn_sync::ArmOutcome outcome,
                           bool join) noexcept {
    using ArmOutcome = push::activity::spawn_sync::ArmOutcome;
    if (outcome == ArmOutcome::suppressed) {
        return;
    }
    const char* result = outcome == ArmOutcome::armed
                             ? "armed"
                             : (outcome == ArmOutcome::rearmed ? "rearmed" : "invalid");
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=activity_spawn_sync stage=arm result=%s reason=%s platform=0x%016llX state=%s",
        result,
        join ? "join" : "transition_token",
        static_cast<unsigned long long>(session.credentials.platformId),
        outcome == ArmOutcome::invalid ? "ready" : "hold");
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         outcome == ArmOutcome::invalid ? core::log::Level::warn
                                                       : core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

ConnectionFields connection_fields(const ServiceOutcome& outcome) noexcept {
    ConnectionFields fields{};
    const auto* plan = transaction_if<activity_message::ActivityPlan>(outcome);
    if (plan == nullptr) {
        return fields;
    }
    fields.activityMessageType = plan->requestMessageType;
    fields.observesActivityMessage = plan->requestMessageType != 0;
    if (plan->delivery == activity_message::Delivery::joinNotifications) {
        fields.joinMemberKey = plan->entitySlotMutation.memberKey;
        fields.joinCharacterSoid = plan->joinCharacterSoid;
        fields.joinSessionId = plan->sessionId;
        fields.joinCorrelation = plan->correlation;
        fields.joinEntitySlots = plan->entitySlotMutation.mask;
        const auto& retirement=plan->entitySlotMutation.retirement;
        fields.retirementJoinRevision=retirement.managed?retirement.join.revision+1:0;
        fields.publishedRetirementSerial=retirement.managed?retirement.join.through:0;
        fields.joinsActivity = true;
    }
    fields.opensTransitionWindow =
        plan->delivery == activity_message::Delivery::joinNotifications || plan->transitionStarted;
    if (plan->mutationDomain == activity_message::MutationDomain::patchEpoch) {
        fields.patchEpoch = plan->patchEpoch;
        fields.retainsPatchEpoch = true;
    }
    if (plan->mutationDomain == activity_message::MutationDomain::membership) {
        fields.membershipKind = plan->membershipMutation.kind;
        if (plan->membershipMutation.kind
            == state::activity::membership::MutationKind::authoritative) {
            fields.authoritativeHasRegion = plan->membershipMutation.authoritativeInput.hasRegion;
            fields.authoritativeRegion =
                plan->membershipMutation.authoritativeInput.region.index;
            fields.authoritativeHasTransitionToken =
                plan->membershipMutation.authoritativeInput.hasTransitionToken;
            fields.authoritativeTransitionToken =
                plan->membershipMutation.authoritativeInput.transitionToken;
        }
    }
    return fields;
}

void publish_connection_fields(Session& session,
                               const transactions::Publication& publication,
                               const ConnectionFields& fields) noexcept {
    if (publication.hasActivitySessionBinding) {
        if (session.activitySessionId != publication.activitySessionId) {
            session.activityContract = {};
            session.activityRetirementJoinRevision = 0;
            session.activityPublishedRetirementSerial = 0;
            session.socialRosterArrival = {};
            // a reused transport must not give the next match an already-expired results deadline
            session.activityCrucibleCheckTick = 0;
            session.activityCrucibleEndTick = 0;
            session.activityCruciblePhase = 0;
            session.activityCrucibleCompletionPhase = 0;
            session.activityCrucibleRevision = 0;
            session.activityForeignMembershipPublished = false;
            session.activityForeignRosterPublished = false;
            session.activityPublicMembershipNotBeforeTick = 0;
            session.activityMembershipBaselineRegion = -1;
            session.activityAdvertisedRegion = -1;
        }
        // only the first binding decides the link's kind; a later join must not reclassify it
        if (session.activitySessionId == 0 && publication.activitySessionFromJoin) {
            session.activityJoinedForeignSession =
                gameplay::group::source_activity_session_for_host(publication.activitySessionId)!=0;
        }
        session.activitySessionId = publication.activitySessionId;
        session.activityContract.allocated = true;
    }
    if (fields.joinMemberKey != 0) {
        session.activityMemberKey = fields.joinMemberKey;
    }
    if (fields.joinCharacterSoid != 0) {
        session.activityCharacterSoid = fields.joinCharacterSoid;
    }
    if (fields.retainsPatchEpoch) {
        session.activityPatchEpoch = fields.patchEpoch;
        session.activityPatchEpochSeen = true;
    }
    if (fields.observesActivityMessage) {
        session.activityContract.lastMessageType = fields.activityMessageType;
    }
    if (fields.joinsActivity) {
        session.activityRetirementJoinRevision=fields.retirementJoinRevision;
        session.activityPublishedRetirementSerial=fields.publishedRetirementSerial;
        session.activityContract.joined = true;
    }
    using MembershipKind = state::activity::membership::MutationKind;
    if (fields.membershipKind == MembershipKind::identity) {
        session.activityContract.identity = true;
    } else if (fields.membershipKind == MembershipKind::authoritative) {
        session.activityContract.authoritative = true;
        if (fields.authoritativeHasRegion && fields.authoritativeRegion >= 0) {
            session.activityContract.region = true;
            session.activityContract.reportedRegion = fields.authoritativeRegion;
        }
        session.activityContract.transitionToken =
            session.activityContract.transitionToken || fields.authoritativeHasTransitionToken;
        if (fields.authoritativeHasTransitionToken) {
            session.activityContract.reportedTransitionToken =
                fields.authoritativeTransitionToken;
        }
    } else if (fields.membershipKind == MembershipKind::refresh) {
        session.activityContract.refresh = true;
    } else if (fields.membershipKind == MembershipKind::acknowledgement) {
        session.activityContract.acknowledgement = true;
    }
    session.activityContract.patchEpoch = session.activityPatchEpochSeen;
    report_contract(session);
    if (fields.membershipKind == MembershipKind::identity
        || fields.membershipKind == MembershipKind::authoritative
        || fields.membershipKind == MembershipKind::refresh) {
        const std::int32_t region =
            push::activity::effective_region(session.activitySessionId).index;
        const auto phase = push::activity::membership_publication_phase(
            true,
            region,
            session.activityMembershipBaselineRegion,
            session.activityAdvertisedRegion);
        if (phase == push::activity::MembershipPublicationPhase::baseline
            || phase == push::activity::MembershipPublicationPhase::advertisement) {
            session.activityKeepaliveDueTick = GetTickCount64();
        }
    }
    if (session.activityJoinedForeignSession
        && (!session.activityForeignMembershipPublished
            || !session.activityForeignRosterPublished)
        && session.activityContract.ready_for_public_target_roster(
            assigned_public_region_ready(session))) {
        session.activityKeepaliveDueTick = GetTickCount64();
    }
    if (fields.opensTransitionWindow) {
        if constexpr (core::runtime::policy::kActivityBackendReplication) {
            const std::uint64_t now = GetTickCount64();
            session.activityTransitionUntilTick = now + kTransitionWindowMs;
            // only the private host link arms the player-wide state; derived public links must not start their own load cycle
            if (!session.activityJoinedForeignSession) {
                const auto armed = push::activity::spawn_sync::arm(
                    session.credentials.platformId, now, fields.joinsActivity,
                    session.activitySessionId);
                report_spawn_sync_arm(session, armed, fields.joinsActivity);
            }
        }
    }
    // WS-701 writes account preferences, not a world-ready heartbeat; a stale type-38 ack must not release this hold
    if (!session.activityJoinedForeignSession
        && push::activity::spawn_sync::awaiting_client_sync(session.credentials.platformId)) {
        std::uint64_t account{};
        const auto context = state::capture_account_runtime(account);
        state::activity::SessionRecord record{};
        if (state::activity::snapshot_for_runtime(context, account, session.activitySessionId, record)
            && push::activity::spawn_sync::join_contract_ready(session, record)
            && push::activity::spawn_sync::acknowledge_join(
                session.credentials.platformId, session.activitySessionId)) {
            session.activityKeepaliveDueTick = GetTickCount64();
            std::array<char, 256> line{};
            const int length = std::snprintf(line.data(), line.size(),
                "ev=activity_spawn_sync stage=native_ack result=released platform=0x%016llX "
                "session=0x%016llX membership_revision=%u state=ready",
                static_cast<unsigned long long>(session.credentials.platformId),
                static_cast<unsigned long long>(session.activitySessionId), record.membership.revision);
            if (length > 0 && static_cast<std::size_t>(length) < line.size())
                core::log::write(core::log::Channel::server, core::log::Level::info,
                    {line.data(), static_cast<std::size_t>(length)});
        }
    }
    // a join resets the client's roster container; the state-byte moves make it rebuild every roster object, and the player binds only then
    if (fields.joinsActivity) {
        session.activityRosterSends = 0;
        session.socialRosterArrival = {};
        session.activityRosterGroups = 0;
        session.activityRosterSpawnSyncRevision = 0;
        session.activityRosterClientSyncKnown = false;
        session.activityRosterAwaitClientSync = false;
        session.activityDirectorSends = 0;
        session.activityMissionDirectorActive = false;

        if constexpr (!core::runtime::policy::kActivityBackendReplication) {
            session.activityTransitionUntilTick = 0;
            session.activityKeepaliveDueTick = GetTickCount64();
            session.activityRosterDueTick = 0;
            return;
        }

        const bool identitySeeded = push::activity::seed_identity(
            session.activitySessionId, session.activityMemberKey, session.activityCharacterSoid);
        const std::int32_t publicTargetRegion =
            session.activityJoinedForeignSession
                ? gameplay::group::region_for_host_session(session.activitySessionId)
                : gameplay::group::kUnknownRegion;
        const bool tokenSeeded =
            publicTargetRegion >= 0
                ? push::activity::seed_transition_region(
                      session.activitySessionId, publicTargetRegion)
                : push::activity::seed_transition_token(session.activitySessionId);
        const std::uint64_t now = GetTickCount64();
        session.activityKeepaliveDueTick = now;
        session.activityRosterDueTick = 0;

        std::array<char, core::log::kLineCapacity> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=activity stage=join_membership_seed result=%s session=0x%llX member=0x%llX "
            "character=0x%llX token=%u target_region=%d region_seed=%u "
            "epoch=client_owned roster=await_client_epoch",
            identitySeeded ? "ok" : "fail",
            static_cast<unsigned long long>(session.activitySessionId),
            static_cast<unsigned long long>(session.activityMemberKey),
            static_cast<unsigned long long>(session.activityCharacterSoid),
            tokenSeeded ? 1U : 0U,
            publicTargetRegion,
            publicTargetRegion >= 0 && tokenSeeded ? 1U : 0U);
        if (written > 0) {
            core::log::write(core::log::Channel::server,
                             identitySeeded ? core::log::Level::info : core::log::Level::warn,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
}

void arm_repushes(Session& session, const queuez::StagedPublication& queuezPublication) noexcept {
    const std::uint64_t now = GetTickCount64();
    if (queuezPublication.foreignAppearance.valid) {
        (void)session.foreignAppearance.full(queuezPublication.foreignAppearance,
                                             now + kBannerRepushDelayMs);
    }
    if (queuezPublication.foreignAppearanceUnsubscribeRoot != 0) {
        const auto root = queuezPublication.foreignAppearanceUnsubscribeRoot;
        session.foreignAppearance.unsubscribe(root);
        if (session.bannerRepushRoot == root) {
            session.bannerRepushArmed = false;
            session.bannerRepushRoot = 0;
            session.bannerRepushDueTick = 0;
        }
    }
    // the subscription reply already carries the full Family-4 snapshot; a second copy 400 ms later left a frame half-read when BAP reset
    session.family4RepushDueTick = 0;
    session.family4RepushRoot = 0;
    session.family4RepushArmed = false;
    if (queuezPublication.armsBannerRepush && queuezPublication.bannerRepushRoot != 0) {
        session.bannerRepushDueTick = now + kBannerRepushDelayMs;
        session.bannerRepushRoot = queuezPublication.bannerRepushRoot;
        session.bannerRepushArmed = true;
    }
    if (queuezPublication.socialRosterRepushRoot != 0) {
        session.socialRosterRepushRoot = queuezPublication.socialRosterRepushRoot;
        (void)session.socialRosterPresence.subscribe(
            queuezPublication.socialRosterRepushRoot,
            roster_activity_index_locked(queuezPublication.socialRosterRepushRoot), now);
    }
    if (queuezPublication.socialRosterUnsubscribeRoot != 0) {
        session.socialRosterPresence.unsubscribe(queuezPublication.socialRosterUnsubscribeRoot);
        if (session.socialRosterRepushRoot == queuezPublication.socialRosterUnsubscribeRoot) {
            session.socialRosterRepushRoot = 0;
            session.socialRosterRepushArmed = false;
        }
    }
    if (queuezPublication.rearmsSocialRosterRepush && session.socialRosterRepushRoot != 0) {
        session.socialRosterRepushDueTick = now + kSocialRosterRepushDelayMs;
        session.socialRosterRepushArmed = true;
    }
    if (queuezPublication.armsJoinRepush && queuezPublication.joinRepushRoot != 0) {
        session.joinRepushDueTick = now + kJoinRepushDelayMs;
        session.joinRepushDeadlineTick = now + kJoinRepushWindowMs;
        session.joinRepushRoot = queuezPublication.joinRepushRoot;
        session.joinRepushArmed = true;
    }
}

} // namespace dawn::server::bap::encrypted
