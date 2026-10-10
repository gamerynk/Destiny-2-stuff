#include "activity_membership_query.h"

#include <Windows.h>

#include "../../runtime/storage/internal.h"
#include "../transactions/internal.h"

namespace dawn::state::activity::membership {

core::protocol::ActivityPlayerTeam player_team(std::uint64_t sessionId) noexcept {
    core::protocol::ActivityPlayerTeam result{};
    if (sessionId==kAbsentSessionId) return result;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state=runtime::storage::activity();
    const auto target=activity::transactions::find_session(state,sessionId);
    if (target!=kInvalidSessionSlot && state.sessions[target].joined
        && state.sessions[target].ownerAccountSoid==runtime::storage::account().primarySoid) {
        result=state.sessions[target].membership.peerConnection.playerTeam;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return result;
}

bool acknowledged(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return false;
    }
    bool applied = false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid) {
        const MembershipState& membership = state.sessions[target].membership;
        applied = membership.revision != kAbsentRevision
                  && membership.acknowledgedRevision == membership.revision;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return applied;
}

std::int32_t reported_region(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return kAbsentRegionIndex;
    }
    std::int32_t region = kAbsentRegionIndex;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid) {
        region = state.sessions[target].membership.region.index;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return region;
}

std::uint64_t live_region_session(std::uint64_t fallback) noexcept {
    std::uint64_t newest = kAbsentSessionId;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const std::uint64_t ownerAccountSoid = runtime::storage::account().primarySoid;
    for (const SessionRecord& record : runtime::storage::activity().sessions) {
        if (record.occupied && record.ownerAccountSoid == ownerAccountSoid
            && record.sessionId > newest
            && record.membership.region.index > kAbsentRegionIndex) {
            newest = record.sessionId;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return newest == kAbsentSessionId ? fallback : newest;
}

std::uint64_t member_key(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return 0;
    }
    std::uint64_t key = 0;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot && state.sessions[target].joined
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid) {
        key = state.sessions[target].memberKey;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return key;
}

std::uint64_t join_identity(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return 0;
    }
    std::uint64_t identity = 0;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot && state.sessions[target].membership.hasIdentity
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid) {
        identity = state.sessions[target].membership.identity.joinIdentity;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return identity;
}

std::uint64_t account_soid(std::uint64_t sessionId) noexcept {
    if (sessionId == kAbsentSessionId) {
        return 0;
    }
    std::uint64_t accountSoid = 0;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target != kInvalidSessionSlot && state.sessions[target].joined
        && state.sessions[target].membership.hasIdentity
        && state.sessions[target].ownerAccountSoid
               == runtime::storage::account().primarySoid) {
        accountSoid = state.sessions[target].membership.identity.accountSoid;
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return accountSoid;
}

} // namespace dawn::state::activity::membership
