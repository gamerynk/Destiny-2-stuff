#include <Windows.h>

#include <cstddef>

#include "../../../runtime/storage/internal.h"
#include "../runtime.h"
#include "internal.h"
#include "../../shared/runtime.h"
#include "../../shared/public_admission.h"

namespace dawn::state::activity::entity_slots {
namespace {

[[nodiscard]] bool retained_owner_rejoin(const SessionRecord& record,
                                         const PendingMutation& prepared) noexcept {
    return record.occupied && !record.joined && record.sharedOwnerDetached
        && record.memberKey != kClearedMemberKey
        && prepared.memberKey == record.memberKey
        && record.sharedOwnerAccountSoid == record.ownerAccountSoid
        && record.sharedOwnerCreatedRevision == record.createdRevision
        && record.createdRevision != kInvalidRevision
        && record.joinedRevision != kInvalidRevision
        && record.sharedMemberSlot == 0 && record.sharedLeaseParts > 1
        && record.sharedLeaseParts <= 32
        && record.membership.hasIdentity
        && record.membership.identity.memberKey == record.memberKey
        && record.membership.identity.accountSoid == record.ownerAccountSoid
        && transactions::empty(record.heldEntitySlots)
        && transactions::equal(prepared.serverMask, record.serverEntitySlots);
}

[[nodiscard]] bool apply(SessionRecord& record, const PendingMutation& prepared) noexcept {
    if (prepared.kind == MutationKind::join) {
        for (std::size_t index = 0; index < prepared.mask.size(); ++index) {
            if ((prepared.serverMask[index] & prepared.mask[index]) != std::byte{}) {
                return false;
            }
        }
        // a second region's activity client grants from a clear set, so it never waits on the
        // first one's slots coming back
        record.heldEntitySlots = prepared.mask;
        record.serverEntitySlots = prepared.serverMask;
        record.memberKey = prepared.memberKey;
        record.membership.peerConnection=prepared.peerConnection;
        record.joined = true;
        record.restoredDormant = false;
        record.sharedOwnerDetached = false;
        const auto parts=shared::lease_parts(record);
        if (parts>1 && record.sharedOwnerAccountSoid==0) {
            record.sharedOwnerAccountSoid=record.ownerAccountSoid;
            record.sharedOwnerCreatedRevision=record.createdRevision;
            record.sharedLeaseParts=parts;
        }
        return true;
    }
    if (!record.joined) {
        return false;
    }
    if (prepared.kind == MutationKind::grant) {
        for (std::size_t index = 0; index < prepared.mask.size(); ++index) {
            const bool overlapsClient =
                (record.heldEntitySlots[index] & prepared.mask[index]) != std::byte{};
            const bool overlapsServer =
                (record.serverEntitySlots[index] & prepared.mask[index]) != std::byte{};
            if (overlapsClient || overlapsServer) {
                return false;
            }
        }
        for (std::size_t index = 0; index < prepared.mask.size(); ++index) {
            record.heldEntitySlots[index] |= prepared.mask[index];
        }
        return true;
    }
    if (prepared.kind != MutationKind::release
        || transactions::exceeds(prepared.mask, record.heldEntitySlots)) {
        return false;
    }
    for (std::size_t index = 0; index < prepared.mask.size(); ++index) {
        record.heldEntitySlots[index] &= ~prepared.mask[index];
    }
    return true;
}

} // namespace

bool commit(PendingMutation& mutation) noexcept {
    if (mutation.importsShared) return shared::commit_join(mutation);
    const PendingMutation prepared = mutation;
    mutation = {};
    if (!prepared.prepared || prepared.peerConnection!=prepared.peerConnectionGuard
        || prepared.kind == MutationKind::none
        || prepared.sessionId == kAbsentSessionId
        || prepared.expectedStateRevision == kInvalidRevision
        || prepared.expectedRecordRevision == kInvalidRevision
        || prepared.targetSlot >= kSessionCapacity) {
        return false;
    }
    const bool countMutation =
        prepared.kind == MutationKind::join || prepared.kind == MutationKind::grant;
    if ((countMutation && !transactions::empty(prepared.returnedMask))
        || (prepared.kind == MutationKind::release && prepared.requestedCount != 0)
        || (prepared.kind == MutationKind::join && prepared.memberKey != prepared.expectedMemberKey)
        || (prepared.kind != MutationKind::join
            && (prepared.memberKey != kClearedMemberKey
                || prepared.expectedMemberKey != kClearedMemberKey
                || !transactions::empty(prepared.serverMask)
                || prepared.serverReserveCount != 0))) {
        return false;
    }

    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const SessionRecord& currentRecord = state.sessions[prepared.targetSlot];
    if (state.stateRevision == kMaximumRevision
        || state.stateRevision != prepared.expectedStateRevision || !currentRecord.occupied
        || currentRecord.ownerAccountSoid != runtime::storage::account().primarySoid
        || currentRecord.sessionId != prepared.sessionId
        || currentRecord.recordRevision != prepared.expectedRecordRevision) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    LeaseMask expected{};
    if (prepared.kind == MutationKind::join) {
        if(currentRecord.publicWorld && !shared::valid_public_join_locked(currentRecord,
            currentRecord.ownerAccountSoid,prepared.memberKey)) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
        if (currentRecord.sharedOwnerAccountSoid!=0
            && prepared.memberKey!=(currentRecord.joined?currentRecord.memberKey:currentRecord.membership.identity.memberKey)) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
        const bool reserveFits =
            prepared.serverReserveCount < kSlotCount
            && prepared.requestedCount <= kSlotCount - prepared.serverReserveCount;
        if (prepared.requestedCount == 0 || !reserveFits || (currentRecord.publicWorld && transactions::empty(prepared.mask))) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
        // a never-joined record must have no key; a detached canonical owner is the exception,
        // and only that exact owner/incarnation/partition may reopen
        const bool retainedOwner = retained_owner_rejoin(currentRecord, prepared);
        if ((currentRecord.sharedOwnerDetached && !retainedOwner)
            || (!currentRecord.joined && currentRecord.memberKey != kClearedMemberKey
                && !retainedOwner)) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
        const LeaseMask reserved = transactions::reserve_high(prepared.serverReserveCount);
        if (!transactions::equal(prepared.serverMask, reserved)) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
        expected = transactions::select_free(shared::allocation_exclusions_locked(currentRecord,prepared.serverReserveCount,true), prepared.requestedCount);
    } else if (prepared.kind == MutationKind::grant) {
        if (prepared.requestedCount == 0 || !currentRecord.joined) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
        expected = transactions::select_free(
            shared::allocation_exclusions_locked(currentRecord,transactions::count(currentRecord.serverEntitySlots),false),
            prepared.requestedCount);
    } else if (prepared.kind == MutationKind::release) {
        if (!currentRecord.joined) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
        expected = transactions::intersect(currentRecord.heldEntitySlots, prepared.returnedMask);
    } else {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    if (!transactions::equal(prepared.mask, expected)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    const auto previousLease=entity_retirement::wire::from_wire_mask(currentRecord.heldEntitySlots);
    ActivityState candidate = state;
    SessionRecord& record = candidate.sessions[prepared.targetSlot];
    const bool wasJoined = record.joined;
    const bool changesState =
        (prepared.kind == MutationKind::join && !wasJoined) || !transactions::empty(prepared.mask);
    if (!apply(record, prepared)) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }
    if (changesState) {
        ++candidate.stateRevision;
        record.recordRevision = candidate.stateRevision;
        if (prepared.kind == MutationKind::join) {
            record.joinedRevision = candidate.stateRevision;
        }
    }
    const auto nextLease=entity_retirement::wire::from_wire_mask(record.heldEntitySlots);
    const bool leaseJournalValid=prepared.kind==MutationKind::join
        || (prepared.retirementLease.activity==prepared.sessionId
            && entity_retirement::can_commit_lease_change_locked(prepared.retirementLease,previousLease,nextLease));
    const bool retirementValid=!prepared.retirement.managed
        || (prepared.kind==MutationKind::join && prepared.retirement.activity==prepared.sessionId
            && prepared.retirement.join.member==prepared.memberKey
            && prepared.retirement.join.lease==entity_retirement::wire::from_wire_mask(prepared.mask)
            && entity_retirement::can_commit_join_locked(prepared.retirement));
    const bool published = leaseJournalValid && retirementValid
        && (!changesState || runtime::storage::publish_activity_locked(candidate));
    if (published) entity_retirement::commit_join_locked(prepared.retirement);
    if (published && prepared.kind!=MutationKind::join)
        entity_retirement::commit_lease_change_locked(prepared.retirementLease,previousLease,nextLease);
    if (published) shared::note_slot_lease_locked(record,prepared.mask,prepared.kind==MutationKind::release);
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return published;
}

} // namespace dawn::state::activity::entity_slots
