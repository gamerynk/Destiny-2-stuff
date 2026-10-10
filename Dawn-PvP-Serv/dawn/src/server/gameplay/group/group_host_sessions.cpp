#include "group_host_sessions.h"

#include <Windows.h>

#include <array>

#include "../../../state/activity/destination/activity_destination_snapshot.h"
#include "../../../state/activity/runtime.h"
#include "../../../state/activity/shared/runtime.h"
#include "../../../state/activity/shared/public_admission.h"
#include "../../../state/runtime/runtime.h"
#include "../gameplay_log.h"
#include "group_host.h"

namespace dawn::server::gameplay::group {

namespace {

struct HostSession {
    struct Arrival {
        state::AccountRuntimeContext context{};
        std::uint64_t account{},source{};
    };
    std::array<Arrival,32> arrivals{};
    std::uint64_t groupSessionId{};
    std::uint64_t migratedGroupSessionId{};
    std::uint64_t hostSessionId{};
    std::uint64_t sourceActivitySessionId{};
    state::AccountRuntimeContext context{};
    std::uint64_t accountSoid{};
    std::uint64_t generation{};
    std::uint64_t lastUse{};
    std::int32_t regionIndex{};
    bool occupied{};
};
void retain_arrival(HostSession& entry,const state::AccountRuntimeContext& context,
                    std::uint64_t account,std::uint64_t source) noexcept {
    if(source==0 || source==entry.hostSessionId || !context.account || !context.activity || account==0) return;
    for(auto& arrival:entry.arrivals) if(arrival.account==account) {arrival={context,account,source};return;}
    for(auto& arrival:entry.arrivals) if(arrival.account==0) {arrival={context,account,source};return;}
}

constexpr std::size_t kHostSessionCapacity = 8;
SRWLOCK g_hostSessionLock{SRWLOCK_INIT};
std::array<HostSession, kHostSessionCapacity> g_hostSessions{};
std::uint64_t g_useStamp = 0;
std::array<HostSession, kHostSessionCapacity> g_evicted{};
std::size_t g_evictedCount = 0;
std::uint64_t g_generation = 0;

[[nodiscard]] bool same_runtime(const state::AccountRuntimeContext& left,
                                const state::AccountRuntimeContext& right) noexcept {
    return left.account == right.account && left.activity == right.activity;
}

[[nodiscard]] HostSession claimed_row(std::uint64_t groupSessionId, std::int32_t regionIndex,
                                      std::uint64_t sourceActivitySessionId,
                                      const state::AccountRuntimeContext& context,
                                      std::uint64_t accountSoid) noexcept {
    HostSession row{};
    row.groupSessionId = groupSessionId;
    row.sourceActivitySessionId = sourceActivitySessionId;
    row.context = context;
    row.accountSoid = accountSoid;
    row.generation = ++g_generation;
    row.lastUse = ++g_useStamp;
    row.regionIndex = regionIndex;
    row.occupied = true;
    return row;
}

[[nodiscard]] bool release_owned_host(const HostSession& entry) noexcept {
    state::activity::SessionRecord record{};
    if (!state::activity::snapshot_for_runtime(
            entry.context, entry.accountSoid, entry.hostSessionId, record)) return false;
    const state::ScopedAccountRuntime scope(entry.context);
    return state::activity::release_session(entry.hostSessionId);
}

// a region's stable descriptor outlives a PRIVATE ActivityClient; once all hosts leave, a
// new launch must not reuse it. Never release State inside a membership push, queue it
[[nodiscard]] bool refresh_retired_source(std::uint64_t groupSessionId,
                                          std::int32_t regionIndex,
                                          std::uint64_t sourceSessionId,
                                          const state::AccountRuntimeContext& context,
                                          std::uint64_t accountSoid) noexcept {
    if (sourceSessionId==state::activity::kAbsentSessionId) return true;
    HostSession previous{};
    AcquireSRWLockShared(&g_hostSessionLock);
    for (const auto& entry:g_hostSessions) {
        if (entry.occupied && (entry.groupSessionId==groupSessionId
                              || entry.migratedGroupSessionId==groupSessionId)) {
            previous=entry;
            break;
        }
    }
    ReleaseSRWLockShared(&g_hostSessionLock);
    // PUBLIC keepalive asks for its own descriptor after PRIVATE admission; the cached host is another
    // name for this binding, not a new PRIVATE source, so keep the original and require the same account
    if (!previous.occupied || previous.hostSessionId==state::activity::kAbsentSessionId
        || ((previous.sourceActivitySessionId==sourceSessionId || previous.hostSessionId==sourceSessionId)
            && previous.accountSoid==accountSoid && same_runtime(previous.context,context))) return true;

    state::activity::SessionRecord next{};
    if (!state::activity::snapshot_for_runtime(context,accountSoid,sourceSessionId,next)
        || !next.joined || next.restoredDormant
        || (previous.sourceActivitySessionId!=state::activity::kAbsentSessionId
            && state::activity::shared::has_joined_participants(previous.context,previous.accountSoid,
                                                               previous.sourceActivitySessionId))
        || state::activity::shared::has_joined_participants(previous.context,previous.accountSoid,
                                                           previous.hostSessionId)) return false;

    bool refreshed=false;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (auto& entry:g_hostSessions) {
        if (entry.occupied && entry.groupSessionId==previous.groupSessionId
            && entry.generation==previous.generation && entry.hostSessionId==previous.hostSessionId
            && g_evictedCount<g_evicted.size()) {
            g_evicted[g_evictedCount++]=entry;
            entry=claimed_row(groupSessionId,regionIndex==kUnknownRegion?previous.regionIndex:regionIndex,
                              sourceSessionId,context,accountSoid);
            refreshed=true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    if (refreshed) report(core::log::Level::info,
        "ev=gameplay stage=activityhost result=source_retired group=0x%016llX old_source=0x%llX new_source=0x%llX",
        static_cast<unsigned long long>(groupSessionId),
        static_cast<unsigned long long>(previous.sourceActivitySessionId),
        static_cast<unsigned long long>(sourceSessionId));
    return refreshed;
}

[[nodiscard]] bool
claim_locked(std::uint64_t groupSessionId,
             std::int32_t regionIndex,
             std::uint64_t sourceActivitySessionId,
             const state::AccountRuntimeContext& context,
             std::uint64_t accountSoid,
             std::uint64_t& held) noexcept {
    held = state::activity::kAbsentSessionId;
    for (HostSession& entry : g_hostSessions) {
        if (entry.occupied
            && (entry.groupSessionId == groupSessionId
                || entry.migratedGroupSessionId == groupSessionId)) {
            entry.lastUse = ++g_useStamp;
            if (regionIndex != kUnknownRegion) {
                entry.regionIndex = regionIndex;
            }
            // readiness can claim the row before the descriptor body is encoded; until the host session is
            // allocated, retain the latest real source so its destination is inherited
            if (entry.hostSessionId == state::activity::kAbsentSessionId
                && sourceActivitySessionId != state::activity::kAbsentSessionId
                && (entry.sourceActivitySessionId == state::activity::kAbsentSessionId
                    || (same_runtime(entry.context, context) && entry.accountSoid == accountSoid))
                && (entry.sourceActivitySessionId != sourceActivitySessionId
                    || !same_runtime(entry.context, context))) {
                entry.sourceActivitySessionId = sourceActivitySessionId;
                entry.context = context;
                entry.accountSoid = accountSoid;
                entry.generation = ++g_generation;
            }
            held = entry.hostSessionId;
            return true;
        }
    }
    for (HostSession& entry : g_hostSessions) {
        if (!entry.occupied) {
            entry = claimed_row(groupSessionId, regionIndex, sourceActivitySessionId,
                                context, accountSoid);
            return true;
        }
    }
    if (g_evictedCount == g_evicted.size()) {
        return false;
    }
    // the evicted session is freed by the service slice: this runs inside a staged push and the release
    // advances the state revision that push commits against
    std::size_t oldest = 0;
    for (std::size_t index = 1; index < g_hostSessions.size(); ++index) {
        if (g_hostSessions[index].lastUse < g_hostSessions[oldest].lastUse) {
            oldest = index;
        }
    }
    if (g_hostSessions[oldest].hostSessionId != state::activity::kAbsentSessionId) {
        g_evicted[g_evictedCount] = g_hostSessions[oldest];
        ++g_evictedCount;
    }
    g_hostSessions[oldest] = claimed_row(groupSessionId, regionIndex, sourceActivitySessionId,
                                       context, accountSoid);
    return true;
}

void free_evicted_host_sessions() noexcept {
    std::array<HostSession, kHostSessionCapacity> freed{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    freed = g_evicted;
    count = g_evictedCount;
    g_evicted = {};
    g_evictedCount = 0;
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    for (std::size_t index = 0; index < count; ++index) {
        const bool released = release_owned_host(freed[index]);
        report(core::log::Level::info,
               "ev=gameplay stage=activityhost result=evicted session=0x%llX state=%s",
               static_cast<unsigned long long>(freed[index].hostSessionId),
               released ? "freed" : "absent");
    }
}

} // namespace

std::uint64_t held_host_session(std::uint64_t groupSessionId) noexcept {
    std::uint64_t held = state::activity::kAbsentSessionId;
    AcquireSRWLockShared(&g_hostSessionLock);
    for (const HostSession& entry : g_hostSessions) {
        if (entry.occupied
            && (entry.groupSessionId == groupSessionId
                || entry.migratedGroupSessionId == groupSessionId)) {
            held = entry.hostSessionId;
            break;
        }
    }
    ReleaseSRWLockShared(&g_hostSessionLock);
    return held;
}

std::uint64_t source_activity_session(std::uint64_t groupSessionId) noexcept {
    std::uint64_t source = state::activity::kAbsentSessionId;
    AcquireSRWLockShared(&g_hostSessionLock);
    for (const HostSession& entry : g_hostSessions) {
        if (entry.occupied
            && (entry.groupSessionId == groupSessionId
                || entry.migratedGroupSessionId == groupSessionId)) {
            source = entry.sourceActivitySessionId;
            break;
        }
    }
    ReleaseSRWLockShared(&g_hostSessionLock);
    return source;
}

bool source_activity_snapshot(std::uint64_t groupSessionId,
                               state::activity::SessionRecord& output) noexcept {
    HostSession source{};
    AcquireSRWLockShared(&g_hostSessionLock);
    for (const HostSession& entry : g_hostSessions) {
        if (entry.occupied && (entry.groupSessionId == groupSessionId
                              || entry.migratedGroupSessionId == groupSessionId)) {
            source = entry;
            break;
        }
    }
    ReleaseSRWLockShared(&g_hostSessionLock);
    const bool found=state::activity::snapshot_for_runtime(
        source.context, source.accountSoid, source.sourceActivitySessionId, output);
    if(found && output.joined) return true;
    state::activity::SessionRecord publicHost{};
    if(state::activity::snapshot_for_runtime(source.context,source.accountSoid,source.hostSessionId,publicHost)
        && publicHost.publicWorld) {output=publicHost;return true;}
    return found;
}

std::uint64_t source_activity_session_for_host(std::uint64_t hostSessionId) noexcept {
    std::uint64_t source = state::activity::kAbsentSessionId;
    if (hostSessionId == state::activity::kAbsentSessionId) {
        return source;
    }
    std::uint64_t accountSoid = 0;
    const auto context = state::capture_account_runtime(accountSoid);
    state::activity::SessionRecord admitted{};
    if(state::activity::shared::public_source_snapshot(hostSessionId,accountSoid,admitted))
        return admitted.sessionId;
    std::array<HostSession,kHostSessionCapacity> candidates{};
    AcquireSRWLockShared(&g_hostSessionLock);
    candidates=g_hostSessions;
    ReleaseSRWLockShared(&g_hostSessionLock);
    for (const HostSession& entry : candidates) {
        state::AccountRuntimeContext canonical{};
        const bool shared=entry.occupied && entry.hostSessionId==hostSessionId
            && (state::activity::shared::canonical_context(entry.sourceActivitySessionId,canonical)
                || state::activity::shared::canonical_context(hostSessionId,canonical))
            && same_runtime(entry.context,canonical);
        if (entry.occupied && entry.hostSessionId == hostSessionId
            && (shared || (entry.accountSoid == accountSoid && same_runtime(entry.context, context)))) {
            source = entry.sourceActivitySessionId;
            break;
        }
    }
    return source;
}

std::int32_t region_for_host_session(std::uint64_t hostSessionId) noexcept {
    std::int32_t region = kUnknownRegion;
    if (hostSessionId == state::activity::kAbsentSessionId) {
        return region;
    }
    std::uint64_t accountSoid = 0;
    const auto context = state::capture_account_runtime(accountSoid);
    state::activity::SessionRecord admitted{};
    const bool publicArrival=state::activity::shared::public_source_snapshot(hostSessionId,accountSoid,admitted);
    std::array<HostSession,kHostSessionCapacity> candidates{};
    AcquireSRWLockShared(&g_hostSessionLock);
    candidates=g_hostSessions;
    ReleaseSRWLockShared(&g_hostSessionLock);
    for (const HostSession& entry : candidates) {
        state::AccountRuntimeContext canonical{};
        const bool shared=entry.occupied && entry.hostSessionId==hostSessionId
            && (state::activity::shared::canonical_context(entry.sourceActivitySessionId,canonical)
                || state::activity::shared::canonical_context(hostSessionId,canonical))
            && same_runtime(entry.context,canonical);
        if (entry.occupied && entry.hostSessionId == hostSessionId
            && (publicArrival || shared || (entry.accountSoid == accountSoid && same_runtime(entry.context, context)))) {
            region = entry.regionIndex;
            break;
        }
    }
    return region;
}

std::uint64_t alias_host_session(std::uint64_t sourceGroupSessionId,
                                 std::uint64_t migratedGroupSessionId) noexcept {
    if (sourceGroupSessionId == 0 || migratedGroupSessionId == 0) {
        return state::activity::kAbsentSessionId;
    }
    std::uint64_t held = state::activity::kAbsentSessionId;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    HostSession* source = nullptr;
    HostSession* conflicting = nullptr;
    for (HostSession& entry : g_hostSessions) {
        if (!entry.occupied) {
            continue;
        }
        if (entry.groupSessionId == sourceGroupSessionId
            || entry.migratedGroupSessionId == sourceGroupSessionId) {
            source = &entry;
        }
        if (entry.groupSessionId == migratedGroupSessionId
            || entry.migratedGroupSessionId == migratedGroupSessionId) {
            conflicting = &entry;
        }
    }
    if (source != nullptr && (conflicting == nullptr || conflicting == source)) {
        source->migratedGroupSessionId = migratedGroupSessionId;
        source->lastUse = ++g_useStamp;
        held = source->hostSessionId;
    }
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    return held;
}

void snapshot_host_sessions(std::span<HostSessionRow> output, std::size_t& count) noexcept {
    count = 0;
    AcquireSRWLockShared(&g_hostSessionLock);
    for (const HostSession& entry : g_hostSessions) {
        if (!entry.occupied || count >= output.size()) {
            continue;
        }
        output[count] = {entry.groupSessionId, entry.hostSessionId, entry.regionIndex};
        ++count;
    }
    ReleaseSRWLockShared(&g_hostSessionLock);
}

std::uint64_t activity_host_session(std::uint64_t groupSessionId,
                                    std::int32_t regionIndex) noexcept {
    bool claimed = false;
    return activity_host_session(
        groupSessionId, regionIndex, state::activity::kAbsentSessionId, claimed);
}

std::uint64_t activity_host_session(std::uint64_t groupSessionId,
                                    std::int32_t regionIndex,
                                    std::uint64_t sourceActivitySessionId,
                                    bool& claimedSlot) noexcept {
    claimedSlot = false;
    if (groupSessionId == 0) {
        return state::activity::kAbsentSessionId;
    }
    // never allocated here: it advances the state revision and would fail the calling push's guard;
    // claim the slot now and let service fill it for the next one
    std::uint64_t held = state::activity::kAbsentSessionId;
    std::uint64_t accountSoid = 0;
    auto context = state::capture_account_runtime(accountSoid);
    const auto callerContext=context;
    const auto callerAccount=accountSoid;
    state::AccountRuntimeContext canonical{};
    if (state::activity::shared::canonical_context(sourceActivitySessionId,canonical)) {
        context=canonical;
        accountSoid=context.account->primarySoid;
    }
    // a PUBLIC region has its own allocator; a compatible independent PRIVATE arrival names that world
    // without replacing its source or joining its Steam lobby
    HostSession publicHost{};
    AcquireSRWLockShared(&g_hostSessionLock);
    for(const auto& entry:g_hostSessions)
        if(entry.occupied && (entry.groupSessionId==groupSessionId || entry.migratedGroupSessionId==groupSessionId))
            {publicHost=entry;break;}
    ReleaseSRWLockShared(&g_hostSessionLock);
    state::activity::SessionRecord host{},arrival{};
    const bool compatible=publicHost.hostSessionId!=0
        && state::activity::snapshot_for_runtime(publicHost.context,publicHost.accountSoid,publicHost.hostSessionId,host)
        && host.publicWorld
        && (regionIndex==kUnknownRegion || publicHost.regionIndex==regionIndex)
        && state::activity::snapshot_for_runtime(callerContext,callerAccount,sourceActivitySessionId,arrival)
        && arrival.joined && !arrival.restoredDormant && sourceActivitySessionId!=host.sessionId
        && state::activity::shared::compatible_public_destination(arrival.destination,host.destination);
    if(compatible) {
        AcquireSRWLockExclusive(&g_hostSessionLock);
        for(auto& entry:g_hostSessions) if(entry.occupied && entry.generation==publicHost.generation
            && entry.groupSessionId==publicHost.groupSessionId && entry.hostSessionId==host.sessionId) {
            retain_arrival(entry,callerContext,callerAccount,sourceActivitySessionId);
            entry.lastUse=++g_useStamp;held=entry.hostSessionId;claimedSlot=true;break;
        }
        ReleaseSRWLockExclusive(&g_hostSessionLock);
        return held;
    }
    if (!refresh_retired_source(groupSessionId,regionIndex,sourceActivitySessionId,context,accountSoid)) {
        // a live host, uncommitted source or in-flight table change can't advertise someone else's
        // authenticated source; let the normal publication retry re-evaluate
        claimedSlot=true;
        return state::activity::kAbsentSessionId;
    }
    AcquireSRWLockExclusive(&g_hostSessionLock);
    const bool claimed =
        claim_locked(groupSessionId, regionIndex, sourceActivitySessionId, context, accountSoid, held);
    if(claimed) for(auto& entry:g_hostSessions)
        if(entry.occupied && (entry.groupSessionId==groupSessionId || entry.migratedGroupSessionId==groupSessionId))
            retain_arrival(entry,callerContext,callerAccount,sourceActivitySessionId);
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    claimedSlot = claimed;
    if (!claimed) {
        report(core::log::Level::warn, "ev=gameplay stage=activityhost result=full");
    }
    return held;
}

void allocate_claimed_host_sessions() noexcept {
    free_evicted_host_sessions();
    std::array<HostSession, kHostSessionCapacity> candidates{};
    AcquireSRWLockShared(&g_hostSessionLock);
    candidates = g_hostSessions;
    ReleaseSRWLockShared(&g_hostSessionLock);
    for (const HostSession& pending : candidates) {
        if(!pending.occupied) continue;
        if(pending.hostSessionId!=state::activity::kAbsentSessionId) {
            for(const auto& arrival:pending.arrivals) if(arrival.source!=0)
                (void)state::activity::shared::admit_public_source(pending.context,pending.hostSessionId,
                    arrival.context,arrival.source);
            continue;
        }
        const state::ScopedAccountRuntime scope(pending.context);
        std::uint64_t sessionId = state::activity::kAbsentSessionId;
        state::activity::PendingAllocation allocation{};
        state::activity::destination::DestinationSelection sourceDestination{};
        const bool inherited = state::activity::destination::snapshot_for_runtime(
            pending.context, pending.accountSoid, pending.sourceActivitySessionId, sourceDestination);
        if (pending.sourceActivitySessionId != state::activity::kAbsentSessionId && !inherited) {
            continue;
        }
        state::activity::SessionRecord parent{};
        const bool hasParent=state::activity::snapshot_for_runtime(pending.context,pending.accountSoid,
            pending.sourceActivitySessionId,parent);
        const bool publicDestination=inherited
            && state::activity::shared::compatible_public_destination(sourceDestination,sourceDestination);
        if(inherited && hasParent && parent.destination!=sourceDestination) continue;
        // a public advertisement can precede the PRIVATE identity notification; don't allocate it as a
        // private/zero-partition host in that gap, re-evaluate on the service slice
        if(publicDestination && (!hasParent || !parent.joined || parent.restoredDormant
            || !parent.membership.hasIdentity || parent.memberKey==0
            || parent.membership.identity.memberKey!=parent.memberKey
            || parent.membership.identity.accountSoid!=pending.accountSoid
            || parent.membership.identity.opaqueSoid==0
            || parent.membership.identity.joinIdentity==0
            || parent.membership.identity.joinIdentity==state::activity::membership::kInvalidJoinIdentity)) continue;
        const bool prepared = inherited
                                  ? state::activity::prepare_session(
                                        sourceDestination, sessionId, allocation)
                                  : state::activity::prepare_session(sessionId, allocation);
        if(prepared && hasParent) {
            state::activity::SessionRecord current{};
            if(!state::activity::snapshot_for_runtime(pending.context,pending.accountSoid,
                pending.sourceActivitySessionId,current) || current.createdRevision!=parent.createdRevision
                || current.recordRevision!=parent.recordRevision) continue;
        }
        bool reservationsReady=true;
        if (prepared && hasParent && (parent.sharedLeaseParts>1 || publicDestination)) {
            std::array<state::activity::membership::Identity,32> members{};
            std::size_t count=1;
            members[0]=state::activity::shared::as_reservation(parent.membership.identity);
            for (const auto& member:parent.membership.reservations)
                if (member.memberKey!=0) members[count++]=member;
            reservationsReady=state::activity::prepare_startup_reservations(allocation,
                std::span(members).first(count));
            if(reservationsReady && publicDestination) {
                allocation.publicLeaseParts=allocation.publicLeasePartsGuard=state::activity::shared::kPublicLeaseParts;
                reservationsReady=state::activity::shared::prepare_public_allocator(allocation);
                sessionId=allocation.sessionId;
            }
        }
        if (!prepared || !reservationsReady || allocation.ownerAccountSoid != pending.accountSoid
            || !state::activity::commit(allocation)) {
            continue;
        }
        bool stored = false;
        std::size_t occupied = 0;
        AcquireSRWLockExclusive(&g_hostSessionLock);
        for (HostSession& entry : g_hostSessions) {
            if (!entry.occupied) {
                continue;
            }
            ++occupied;
            if (entry.groupSessionId == pending.groupSessionId
                && entry.generation == pending.generation
                && entry.hostSessionId == state::activity::kAbsentSessionId) {
                entry.hostSessionId = sessionId;
                stored = true;
            }
        }
        ReleaseSRWLockExclusive(&g_hostSessionLock);
        if (!stored) {
            static_cast<void>(state::activity::release_session(sessionId));
            continue;
        }
        report(core::log::Level::info,
               "ev=gameplay stage=activityhost result=allocated session=0x%llX group=0x%016llX "
               "source=0x%llX inherited=%u held=%zu account=0x%llX",
               static_cast<unsigned long long>(sessionId),
               static_cast<unsigned long long>(pending.groupSessionId),
               static_cast<unsigned long long>(pending.sourceActivitySessionId),
               inherited ? 1U : 0U,
               occupied,
               static_cast<unsigned long long>(pending.accountSoid));
    }
}

void reset_host_sessions() noexcept {
    // dropping the table alone would strand its records in State, and nothing else can name them once
    // their group session is forgotten
    std::array<HostSession, kHostSessionCapacity> held{};
    std::size_t count = 0;
    AcquireSRWLockExclusive(&g_hostSessionLock);
    for (const HostSession& entry : g_hostSessions) {
        if (entry.occupied) {
            held[count] = entry;
            ++count;
        }
    }
    g_hostSessions = {};
    ReleaseSRWLockExclusive(&g_hostSessionLock);
    for (std::size_t index = 0; index < count; ++index) {
        static_cast<void>(release_owned_host(held[index]));
    }
    free_evicted_host_sessions();
}

} // namespace dawn::server::gameplay::group
