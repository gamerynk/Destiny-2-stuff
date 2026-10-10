#include <Windows.h>

#include <cstddef>
#include <cstdint>

#include "../../../runtime/storage/internal.h"
#include "../../transactions/internal.h"
#include "../runtime.h"
#include "internal.h"
#include "../../shared/runtime.h"
#include "../../shared/public_admission.h"

namespace dawn::state::activity::entity_slots {
namespace {

[[nodiscard]] const SessionRecord* prepare_base(const ActivityState& state,
                                                std::uint64_t ownerAccountSoid,
                                                std::uint64_t sessionId,
                                                bool requireJoined,
                                                PendingMutation& mutation) noexcept {
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target == kInvalidSessionSlot || state.stateRevision == kMaximumRevision) {
        return nullptr;
    }
    const SessionRecord& record = state.sessions[target];
    if (record.recordRevision == kInvalidRevision || record.ownerAccountSoid != ownerAccountSoid
        || (requireJoined && !record.joined)) {
        return nullptr;
    }
    mutation.sessionId = sessionId;
    mutation.expectedStateRevision = state.stateRevision;
    mutation.expectedRecordRevision = record.recordRevision;
    mutation.targetSlot = target;
    mutation.prepared = true;
    return &record;
}

} // namespace

bool prepare_join(std::uint64_t sessionId,
                  std::uint64_t memberKey,
                  std::size_t grantCount,
                  std::size_t serverReserveCount,
                  PendingMutation& mutation) noexcept {
    mutation = {};
    if (sessionId == kAbsentSessionId || grantCount == 0 || grantCount > kSlotCount
        || serverReserveCount >= kSlotCount || grantCount > kSlotCount - serverReserveCount) {
        return false;
    }

    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    PendingMutation prepared{};
    const SessionRecord* record =
        prepare_base(state, runtime::storage::account().primarySoid, sessionId, false, prepared);
    if (record && record->sharedOwnerAccountSoid!=0
        && memberKey!=(record->joined?record->memberKey:record->membership.identity.memberKey)) record=nullptr;
    if(record && record->publicWorld
        && !shared::valid_public_join_locked(*record,record->ownerAccountSoid,memberKey)) record=nullptr;
    if (record != nullptr) {
        // each region gets its own activity client and key, so keeping the previous held set
        // would leave the new one ungranted
        prepared.serverMask = transactions::reserve_high(serverReserveCount);
        prepared.mask = transactions::select_free(shared::allocation_exclusions_locked(*record, serverReserveCount,true), grantCount);
        if (record->publicWorld && transactions::empty(prepared.mask)) record=nullptr;
        prepared.memberKey = memberKey;
        prepared.expectedMemberKey = memberKey;
        prepared.requestedCount = grantCount;
        prepared.serverReserveCount = serverReserveCount;
        prepared.kind = MutationKind::join;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (record == nullptr) {
        return false;
    }
    mutation = prepared;
    return true;
}

bool prepare_grant(std::uint64_t sessionId,
                   std::size_t requestedCount,
                   PendingMutation& mutation) noexcept {
    mutation = {};
    if (sessionId == kAbsentSessionId || requestedCount == 0) {
        return false;
    }

    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    PendingMutation prepared{};
    const SessionRecord* record =
        prepare_base(state, runtime::storage::account().primarySoid, sessionId, true, prepared);
    if (record != nullptr) {
        // a later grant may never reach into the server complement
        prepared.mask = transactions::select_free(
            shared::allocation_exclusions_locked(*record, transactions::count(record->serverEntitySlots),false),
            requestedCount);
        prepared.requestedCount = requestedCount;
        prepared.kind = MutationKind::grant;
        const auto after=transactions::unite(record->heldEntitySlots,prepared.mask);
        if (!entity_retirement::prepare_lease_change_locked(sessionId,record->ownerAccountSoid,
            record->memberKey,entity_retirement::wire::from_wire_mask(record->heldEntitySlots),
            entity_retirement::wire::from_wire_mask(after),prepared.retirementLease)) record=nullptr;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (record == nullptr) {
        return false;
    }
    mutation = prepared;
    return true;
}

bool prepare_release(std::uint64_t sessionId,
                     const LeaseMask& returned,
                     PendingMutation& mutation) noexcept {
    mutation = {};
    if (sessionId == kAbsentSessionId) {
        return false;
    }

    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    PendingMutation prepared{};
    const SessionRecord* record =
        prepare_base(state, runtime::storage::account().primarySoid, sessionId, true, prepared);
    if (record != nullptr) {
        prepared.mask = transactions::intersect(record->heldEntitySlots, returned);
        prepared.returnedMask = returned;
        prepared.kind = MutationKind::release;
        auto after=record->heldEntitySlots;
        for (std::size_t i=0;i<after.size();++i) after[i]&=~prepared.mask[i];
        if (!entity_retirement::prepare_lease_change_locked(sessionId,record->ownerAccountSoid,
            record->memberKey,entity_retirement::wire::from_wire_mask(record->heldEntitySlots),
            entity_retirement::wire::from_wire_mask(after),prepared.retirementLease)) record=nullptr;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (record == nullptr) {
        return false;
    }
    mutation = prepared;
    return true;
}

} // namespace dawn::state::activity::entity_slots
