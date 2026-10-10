#include <Windows.h>

#include "../../runtime/runtime.h"
#include "../../runtime/storage/internal.h"
#include "../destination/activity_destination_validation.h"
#include "../runtime.h"
#include "internal.h"
#include "activity_startup_reservations.h"
#include "../shared/public_admission.h"

namespace dawn::state::activity {
namespace {

constexpr std::uint64_t kAccountMask = 0xFFFFFFFF00000000ULL;

[[nodiscard]] std::uint64_t session_owner_account() noexcept {
    return account_snapshot().primarySoid;
}

[[nodiscard]] bool prepare_locked(const ActivityState& state,
                                  const destination::DestinationSelection& selection,
                                  std::uint64_t ownerAccountSoid,
                                  std::uint64_t soidBase,
                                  std::uint64_t& sessionId,
                                  PendingAllocation& allocation) noexcept {
    if (ownerAccountSoid == 0 || !destination::valid(selection)
        || !transactions::allocation_available(state)) {
        return false;
    }

    PendingAllocation prepared{};
    prepared.destination = selection;
    // the allocator counter stays the record index; only the published soid carries account half and class
    prepared.soidBase = soidBase;
    prepared.ownerAccountSoid = ownerAccountSoid;
    prepared.selectedSessionCounter=shared::available_session_counter_locked(soidBase,state.nextSessionId,false);
    if(prepared.selectedSessionCounter==0) return false;
    prepared.sessionId = transactions::compose_session_soid(soidBase, prepared.selectedSessionCounter);
    prepared.expectedStateRevision = state.stateRevision;
    prepared.expectedAllocatorRevision = state.allocatorRevision;
    prepared.expectedNextSessionId = state.nextSessionId;
    prepared.targetSlot = transactions::select_target(state);
    prepared.prepared = true;
    sessionId = prepared.sessionId;
    allocation = prepared;
    return true;
}

} // namespace

bool prepare_startup_reservations(PendingAllocation& allocation,
    std::span<const membership::Identity> peers) noexcept {
    if (!allocation.prepared || allocation.ownerAccountSoid==0 || peers.size()>32
        || account_snapshot().primarySoid!=allocation.ownerAccountSoid) return false;
    PendingAllocation staged=allocation;
    staged.startupReservations={};
    staged.startupOwner={};
    std::size_t count=0;
    for (std::size_t i=0;i<peers.size();++i) {
        const auto& peer=peers[i];
        if (peer.memberKey==0 || peer.memberKey==membership::kInvalidOpaqueSoid
            || peer.smallOpaque!=-1 || peer.accountSoid==0 || peer.opaqueSoid==0) return false;
        for (std::size_t j=0;j<i;++j) if (peers[j].memberKey==peer.memberKey) return false;
        if (peer.accountSoid==allocation.ownerAccountSoid) {
            if (staged.startupOwner.memberKey!=0) return false;
            staged.startupOwner=peer;
            continue;
        }
        if (count==staged.startupReservations.size()) return false;
        staged.startupReservations[count++]=peer;
    }
    staged.startupReservationsGuard=staged.startupReservations;
    staged.startupOwnerGuard=staged.startupOwner;
    if (!transactions::valid_startup_reservations(staged)) return false;
    allocation=staged;
    return true;
}

bool prepare_session(std::uint64_t& sessionId, PendingAllocation& allocation) noexcept {
    sessionId = kAbsentSessionId;
    allocation = {};
    const std::uint64_t ownerAccountSoid = session_owner_account();
    const std::uint64_t soidBase = ownerAccountSoid & kAccountMask;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const ActivityState& state = runtime::storage::activity();
    const bool ready = prepare_locked(
        state,
        state.defaults.defaultDestination.selection,
        ownerAccountSoid,
        soidBase,
        sessionId,
        allocation);
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ready;
}

bool prepare_session(const destination::DestinationSelection& selection,
                     std::uint64_t& sessionId,
                     PendingAllocation& allocation) noexcept {
    sessionId = kAbsentSessionId;
    allocation = {};
    if (!destination::valid(selection)) {
        return false;
    }

    const std::uint64_t ownerAccountSoid = session_owner_account();
    const std::uint64_t soidBase = ownerAccountSoid & kAccountMask;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const bool ready = prepare_locked(
        runtime::storage::activity(),
        selection,
        ownerAccountSoid,
        soidBase,
        sessionId,
        allocation);
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    return ready;
}

} // namespace dawn::state::activity
