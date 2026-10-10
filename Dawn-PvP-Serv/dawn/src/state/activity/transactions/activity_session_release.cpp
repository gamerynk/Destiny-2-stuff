#include "../../runtime/storage/internal.h"
#include "../runtime.h"
#include "internal.h"

namespace dawn::state::activity {

bool release_session(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t slot = transactions::find_session(state, sessionId);
    const bool released = slot < kSessionCapacity
                          && state.sessions[slot].ownerAccountSoid
                                 == runtime::storage::account().primarySoid;
    if (released) {
        ActivityState candidate = state;
        // the revision bump retires every plan prepared against this record
        candidate.sessions[slot] = {};
        ++candidate.stateRevision;
        if (!runtime::storage::publish_activity_locked(candidate)) {
            ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
            return false;
        }
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
    return released;
}

} // namespace dawn::state::activity
