#include <Windows.h>

#include "../runtime/storage/internal.h"
#include "runtime.h"

namespace dawn::state::activity {

bool snapshot_for_runtime(const AccountRuntimeContext& context,
                          std::uint64_t accountSoid,
                          std::uint64_t sessionId,
                          SessionRecord& output) noexcept {
    output = {};
    if (context.account == nullptr || context.activity == nullptr
        || accountSoid == 0 || sessionId == kAbsentSessionId) return false;
    bool found = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    if (context.account->primarySoid == accountSoid) {
        for (const SessionRecord& record : context.activity->sessions) {
            if (record.occupied && record.ownerAccountSoid == accountSoid
                && record.sessionId == sessionId) {
                output = record;
                found = true;
                break;
            }
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return found;
}

bool contains(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::uint64_t ownerAccountSoid = runtime::storage::account().primarySoid;
    bool found = false;
    for (const SessionRecord& record : state.sessions) {
        if (record.occupied && record.ownerAccountSoid == ownerAccountSoid
            && record.sessionId == sessionId) {
            found = true;
            break;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return found;
}

bool is_joined(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::uint64_t ownerAccountSoid = runtime::storage::account().primarySoid;
    bool joined = false;
    for (const SessionRecord& record : state.sessions) {
        if (record.occupied && record.ownerAccountSoid == ownerAccountSoid
            && record.sessionId == sessionId) {
            joined = record.joined && record.joinedRevision != kInvalidRevision;
            break;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return joined;
}

std::uint64_t newest_joined_session() noexcept {
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::uint64_t ownerAccountSoid = runtime::storage::account().primarySoid;
    std::uint64_t selectedId = kAbsentSessionId;
    std::uint64_t selectedRevision = kInvalidRevision;
    for (const SessionRecord& record : state.sessions) {
        if (record.occupied && record.ownerAccountSoid == ownerAccountSoid && record.joined
            && record.joinedRevision != kInvalidRevision
            && record.createdRevision >= selectedRevision) {
            selectedId = record.sessionId;
            selectedRevision = record.createdRevision;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return selectedId;
}

} // namespace dawn::state::activity
