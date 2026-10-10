#include <Windows.h>

#include "../../../runtime/storage/internal.h"
#include "../../transactions/internal.h"
#include "../runtime.h"

namespace dawn::state::activity::bubble_authority {

bool select_grant(std::uint64_t sessionId, std::int32_t sliceSetIndex, Grant& grant) noexcept {
    grant = {};
    if (sessionId == kAbsentSessionId || sliceSetIndex < 0
        || sliceSetIndex > kMaximumGrantSliceSetIndex) {
        return false;
    }
    const auto bubble = static_cast<std::uint8_t>(sliceSetIndex >> kSliceSetToBubbleShift);
    bool owed = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot && bubble < kFallbackBubble
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid
        && state.sessions[target].bubbleAuthority.grantTokens[bubble] == 0) {
        grant.bubble = bubble;
        grant.token = kInitialGrantToken;
        owed = true;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return owed;
}

void record_grant(std::uint64_t sessionId, const Grant& grant) noexcept {
    if (sessionId == kAbsentSessionId || grant.bubble >= kAuthoritySlotCount || grant.token == 0) {
        return;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid
        && state.sessions[target].bubbleAuthority.grantTokens[grant.bubble] != grant.token) {
        ActivityState candidate = state;
        candidate.sessions[target].bubbleAuthority.grantTokens[grant.bubble] = grant.token;
        static_cast<void>(runtime::storage::publish_activity_locked(candidate));
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
}

void clear_grants(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return;
    }
    AcquireSRWLockExclusive(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid) {
        ActivityState candidate = state;
        candidate.sessions[target].bubbleAuthority = {};
        static_cast<void>(runtime::storage::publish_activity_locked(candidate));
    }
    ReleaseSRWLockExclusive(&runtime::storage::g_stateLock);
}

} // namespace dawn::state::activity::bubble_authority
