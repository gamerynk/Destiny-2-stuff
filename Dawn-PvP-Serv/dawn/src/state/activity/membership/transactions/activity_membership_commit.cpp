#include <Windows.h>

#include <cstdint>

#include "../../../runtime/storage/internal.h"
#include "../activity_membership_query.h"
#include "internal.h"
#include "../../shared/runtime.h"

namespace dawn::state::activity::membership {
namespace {

[[nodiscard]] bool commit_identity(ActivityState& state,
                                   SessionRecord& record,
                                   const PendingMutation& prepared) noexcept {
    if (!prepared.hasSnapshot
        || !transactions::equal(prepared.snapshot.identity, prepared.identityGuard)
        || !transactions::valid_identity(prepared.snapshot.identity, record.memberKey)
        || !shared::valid_identity(record,prepared.snapshot.identity)) {
        return false;
    }
    // current State decides, not a comparison with the prepared plan; refusing on a difference
    // would drop the identity whenever State moved between prepare and commit
    const bool changed =
        !record.membership.hasIdentity
        || !transactions::equal(record.membership.identity, prepared.snapshot.identity);
    if (changed
        && (state.stateRevision == activity::kMaximumRevision
            || record.membership.revision == kMaximumMembershipRevision)) {
        return false;
    }
    if (!changed) {
        return true;
    }
    const std::uint32_t revision =
        record.membership.hasIdentity ? record.membership.revision + 1U : kInitialRevision;

    MembershipState updated = record.membership;
    if (!updated.hasTransitionToken) {
        updated.transitionToken = kInitialTransitionToken;
        updated.hasTransitionToken = true;
    }
    updated.identity = prepared.snapshot.identity;
    updated.revision = revision;
    updated.acknowledgedRevision = kAbsentRevision;
    updated.hasIdentity = true;
    record.membership = updated;
    transactions::publish_change(state, record);
    return true;
}

[[nodiscard]] bool commit_refresh(const SessionRecord& record,
                                  const PendingMutation& prepared) noexcept {
    if (prepared.refreshRequestGuard
        != transactions::refresh_guard(prepared.requestedRevision, prepared.bubbleIndex)) {
        return false;
    }
    if (!record.membership.hasIdentity) {
        return !prepared.hasSnapshot;
    }
    if (!prepared.hasSnapshot) {
        return false;
    }
    const Snapshot expected = transactions::make_snapshot(
        record.membership, record.membership.identity, record.membership.revision);
    return transactions::equal(prepared.snapshot, expected);
}

[[nodiscard]] bool commit_acknowledgement(ActivityState& state,
                                          SessionRecord& record,
                                          const PendingMutation& prepared) noexcept {
    const bool changed = record.membership.hasIdentity
                         && prepared.acknowledgement == record.membership.revision
                         && prepared.acknowledgement != record.membership.acknowledgedRevision;
    if (changed && state.stateRevision == activity::kMaximumRevision) {
        return false;
    }
    if (changed) {
        record.membership.acknowledgedRevision = prepared.acknowledgement;
        transactions::publish_change(state, record);
    }
    return true;
}

} // namespace

bool republish(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    bool advanced = false;
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    ActivityState candidate = state;
    for (SessionRecord& record : candidate.sessions) {
        if (!record.occupied || !record.joined || record.sessionId != sessionId) {
            continue;
        }
        if (record.ownerAccountSoid != runtime::storage::account().primarySoid) {
            break;
        }
        if (record.membership.hasIdentity && state.stateRevision != activity::kMaximumRevision
            && record.membership.revision != kMaximumMembershipRevision) {
            ++record.membership.revision;
            record.membership.acknowledgedRevision = kAbsentRevision;
            transactions::publish_change(candidate, record);
            advanced = runtime::storage::publish_activity_locked(candidate);
        }
        break;
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return advanced;
}

bool commit(PendingMutation& mutation) noexcept {
    const PendingMutation prepared = mutation;
    mutation = {};
    if (!prepared.prepared || prepared.kind == MutationKind::none
        || prepared.sessionId == kAbsentSessionId
        || prepared.expectedStateRevision == kInvalidRevision
        || prepared.expectedRecordRevision == kInvalidRevision
        || prepared.targetSlot >= kSessionCapacity) {
        return false;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const SessionRecord& currentRecord = state.sessions[prepared.targetSlot];
    bool committed = state.stateRevision == prepared.expectedStateRevision
                     && currentRecord.occupied
                     && currentRecord.joined
                     && currentRecord.joinedRevision != kInvalidRevision
                     && currentRecord.ownerAccountSoid == runtime::storage::account().primarySoid
                     && currentRecord.sessionId == prepared.sessionId
                     && currentRecord.recordRevision == prepared.expectedRecordRevision
                     && runtime::storage::account().primarySoid == prepared.expectedPrimarySoid;
    ActivityState candidate = state;
    SessionRecord& record = candidate.sessions[prepared.targetSlot];
    if (committed && prepared.kind == MutationKind::identity) {
        committed = commit_identity(candidate, record, prepared);
    } else if (committed && prepared.kind == MutationKind::authoritative) {
        committed = transactions::commit_authoritative(candidate, record, prepared);
    } else if (committed && prepared.kind == MutationKind::refresh) {
        committed = commit_refresh(record, prepared);
    } else if (committed && prepared.kind == MutationKind::acknowledgement) {
        committed = commit_acknowledgement(candidate, record, prepared);
    } else if (committed && prepared.kind == MutationKind::reservations) {
        committed=prepared.hasSnapshot && record.membership.hasIdentity;
        bool changed=false;
        for (std::size_t i=0;i<prepared.reservationsGuard.size();++i) {
            committed=committed && transactions::equal(prepared.snapshot.reservations[i],
                                                       prepared.reservationsGuard[i]);
            changed=changed||!transactions::equal(record.membership.reservations[i],
                                                   prepared.reservationsGuard[i]);
            // a client reservation plan cannot revoke a server-owned social admission, even if the
            // snapshot and its copy were tampered together
            if (record.socialWorld && record.membership.reservations[i].memberKey!=0)
                committed=committed && transactions::equal(record.membership.reservations[i],
                                                            prepared.reservationsGuard[i]);
        }
        committed=committed && changed==prepared.changesState;
        auto expected=transactions::make_snapshot(record.membership,record.membership.identity,
            record.membership.revision+(changed?1U:0U));
        expected.reservations=prepared.reservationsGuard;
        committed=committed && transactions::equal(expected,prepared.snapshot);
        committed=committed && shared::private_reservations_locked(record,expected.reservations,false);
        if (committed && changed) {
            committed=state.stateRevision!=activity::kMaximumRevision
                && record.membership.revision!=kMaximumMembershipRevision;
            if (committed) {
                record.membership.reservations=prepared.reservationsGuard;
                ++record.membership.revision;
                record.membership.acknowledgedRevision=kAbsentRevision;
                transactions::publish_change(candidate,record);
            }
        }
    } else {
        committed = false;
    }
    if (committed && candidate.stateRevision != state.stateRevision) {
        committed = runtime::storage::publish_activity_locked(candidate);
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return committed;
}

} // namespace dawn::state::activity::membership
