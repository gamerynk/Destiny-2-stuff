#include "../../runtime/storage/internal.h"
#include "../destination/activity_destination_validation.h"
#include "../runtime.h"
#include "internal.h"
#include "activity_startup_reservations.h"
#include "../shared/runtime.h"
#include "../shared/public_admission.h"
#include "../shared/social_admission.h"
#include "../../runtime/runtime.h"
#include <algorithm>

namespace dawn::state::activity {

bool commit(PendingAllocation& allocation) noexcept {
    // take the plan first so no transaction can replay, pass or fail
    const PendingAllocation prepared = allocation;
    allocation = {};
    if (prepared.socialSessionId!=0) return shared::commit_social_allocation(prepared);
    if (!prepared.prepared || prepared.sessionId == kAbsentSessionId
        || prepared.expectedStateRevision == kInvalidRevision
        || prepared.expectedAllocatorRevision == kInvalidRevision
        || prepared.ownerAccountSoid == 0
        || prepared.sessionId
               != transactions::compose_session_soid(prepared.soidBase,
                                                     prepared.selectedSessionCounter)
        || prepared.selectedSessionCounter==0
        || prepared.targetSlot >= kSessionCapacity || !destination::valid(prepared.destination)
        || !transactions::valid_startup_reservations(prepared)
        || prepared.socialWorld!=prepared.socialWorldGuard
        || (prepared.socialWorld && (prepared.publicLeaseParts!=0 || !shared::social_destination(prepared.destination)
            || prepared.startupOwner.memberKey==0 || prepared.startupOwner.opaqueSoid==0))
        || prepared.publicLeaseParts!=prepared.publicLeasePartsGuard
        || (prepared.publicLeaseParts!=0 && (prepared.publicLeaseParts!=shared::kPublicLeaseParts
            || prepared.startupOwner.memberKey==0 || prepared.startupOwner.opaqueSoid==0))) {
        return false;
    }

    std::uint64_t contextAccount{};
    const auto context = capture_account_runtime(contextAccount);
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    if (!shared::context_available_locked(context) || !transactions::allocation_available(state)
        || runtime::storage::account().primarySoid != prepared.ownerAccountSoid
        || state.stateRevision != prepared.expectedStateRevision
        || state.allocatorRevision != prepared.expectedAllocatorRevision
        || state.nextSessionId != prepared.expectedNextSessionId
        || shared::available_session_counter_locked(prepared.soidBase,state.nextSessionId,
            prepared.publicLeaseParts!=0 || prepared.socialWorld)!=prepared.selectedSessionCounter
        || (prepared.socialWorld && !shared::social_allocation_valid_locked(prepared))) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    // pick again under the write lock so stale plan data cannot redirect State
    const std::size_t target = transactions::select_target(state);
    if (target != prepared.targetSlot) {
        ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
        return false;
    }

    ActivityState candidate = state;
    SessionRecord& record = candidate.sessions[target];
    record = {};
    ++candidate.stateRevision;
    record.destination = prepared.destination;
    record.sessionId = prepared.sessionId;
    record.ownerAccountSoid = prepared.ownerAccountSoid;
    record.membership.reservations = prepared.startupReservations;
    record.createdRevision = candidate.stateRevision;
    record.recordRevision = candidate.stateRevision;
    record.occupied = true;
    const auto parts=(prepared.publicLeaseParts!=0 || prepared.socialWorld)?shared::kPublicLeaseParts
        :prepared.startupOwner.memberKey!=0
            ?(std::max)(shared::kPrivateLeaseParts,shared::lease_parts(record))
            :shared::lease_parts(record);
    if (parts>1 && prepared.startupOwner.memberKey!=0) {
        record.sharedOwnerAccountSoid=record.ownerAccountSoid;
        record.sharedOwnerCreatedRevision=record.createdRevision;
        record.sharedLeaseParts=parts;
        record.publicWorld=prepared.publicLeaseParts!=0;
        record.socialWorld=prepared.socialWorld;
        // an admission identity is not a joined player; hasIdentity stays false until join
        record.membership.identity=prepared.startupOwner;
    }
    candidate.nextSessionId=prepared.selectedSessionCounter;
    transactions::advance_allocator(candidate);
    const bool published = runtime::storage::publish_activity_locked(candidate);
    if (published) shared::register_context_locked(context);
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return published;
}

} // namespace dawn::state::activity
