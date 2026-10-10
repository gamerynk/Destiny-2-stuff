#include <Windows.h>

#include <cstdint>

#include "../../../runtime/storage/internal.h"
#include "../activity_membership_query.h"
#include "internal.h"
#include "../../shared/runtime.h"

namespace dawn::state::activity::membership {

bool prepare_identity(std::uint64_t sessionId,
                      const Identity& identity,
                      PendingMutation& mutation) noexcept {
    mutation = {};
    if (sessionId == kAbsentSessionId) {
        return false;
    }

    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    PendingMutation prepared{};
    const SessionRecord* record =
        transactions::prepare_base(state, runtime::storage::account().primarySoid, sessionId, prepared);
    bool ready = record != nullptr && transactions::valid_identity(identity, record->memberKey)
        && shared::valid_identity(*record,identity);
    if (ready) {
        const bool changed = !record->membership.hasIdentity
                             || !transactions::equal(record->membership.identity, identity);
        if (changed
            && (state.stateRevision == activity::kMaximumRevision
                || record->membership.revision == kMaximumMembershipRevision)) {
            ready = false;
        } else {
            const std::uint32_t revision =
                !changed ? record->membership.revision
                         : (record->membership.hasIdentity ? record->membership.revision + 1U
                                                           : kInitialRevision);
            prepared.snapshot = transactions::make_snapshot(record->membership, identity, revision);
            prepared.identityGuard = identity;
            prepared.kind = MutationKind::identity;
            prepared.hasSnapshot = true;
            prepared.changesState = changed;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (!ready) {
        return false;
    }
    mutation = prepared;
    return true;
}

bool prepare_refresh(std::uint64_t sessionId,
                     std::uint32_t requestedRevision,
                     std::int32_t bubbleIndex,
                     PendingMutation& mutation) noexcept {
    mutation = {};
    if (sessionId == kAbsentSessionId) {
        return false;
    }

    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    PendingMutation prepared{};
    const SessionRecord* record =
        transactions::prepare_base(state, runtime::storage::account().primarySoid, sessionId, prepared);
    if (record != nullptr) {
        if (record->membership.hasIdentity) {
            prepared.snapshot = transactions::make_snapshot(
                record->membership, record->membership.identity, record->membership.revision);
            prepared.hasSnapshot = true;
        }
        prepared.requestedRevision = requestedRevision;
        prepared.bubbleIndex = bubbleIndex;
        prepared.refreshRequestGuard = transactions::refresh_guard(requestedRevision, bubbleIndex);
        prepared.kind = MutationKind::refresh;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (record == nullptr) {
        return false;
    }
    mutation = prepared;
    return true;
}

bool prepare_acknowledgement(std::uint64_t sessionId,
                             std::uint32_t revision,
                             PendingMutation& mutation) noexcept {
    mutation = {};
    if (sessionId == kAbsentSessionId) {
        return false;
    }

    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    PendingMutation prepared{};
    const SessionRecord* record =
        transactions::prepare_base(state, runtime::storage::account().primarySoid, sessionId, prepared);
    bool ready = record != nullptr;
    if (ready) {
        const bool changed = record->membership.hasIdentity
                             && revision == record->membership.revision
                             && revision != record->membership.acknowledgedRevision;
        if (changed && state.stateRevision == activity::kMaximumRevision) {
            ready = false;
        } else {
            prepared.acknowledgement = revision;
            prepared.kind = MutationKind::acknowledgement;
            prepared.changesState = changed;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (!ready) {
        return false;
    }
    mutation = prepared;
    return true;
}

} // namespace dawn::state::activity::membership
