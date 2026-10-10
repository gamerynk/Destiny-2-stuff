#include <Windows.h>
#include <algorithm>
#include "../../../runtime/storage/internal.h"
#include "internal.h"
#include "../../shared/runtime.h"

namespace dawn::state::activity::membership {
bool prepare_reservations(std::uint64_t sessionId, std::uint32_t epoch,
    std::span<const Identity> peers, std::uint64_t releaseKey, PendingMutation& mutation) noexcept {
    mutation={};
    if (sessionId==kAbsentSessionId || epoch!=kStableEpoch || peers.size()>32
        || (releaseKey!=0 && !peers.empty())) return false;
    AcquireSRWLockShared(&runtime::storage::g_stateLock);
    const auto& state=runtime::storage::activity();
    PendingMutation prepared{};
    const auto* record=transactions::prepare_base(state,
        runtime::storage::account().primarySoid,sessionId,prepared);
    bool ready=record && record->membership.hasIdentity;
    if (ready) {
        prepared.snapshot=transactions::make_snapshot(record->membership,
            record->membership.identity,record->membership.revision);
        auto& rows=prepared.snapshot.reservations;
        ready=shared::private_reservations_locked(*record,rows,true);
        // peers outside the visitor's posse are released by retail on sight; that must not
        // revoke their activity join. Real departure removes it via shared::disconnect
        if (releaseKey!=0 && !record->socialWorld) {
            for (auto& row:rows) if (row.memberKey==releaseKey) row={};
        }
        for (const auto& peer:peers) {
            if(!ready)break;
            if (peer.memberKey==0 || peer.memberKey==kInvalidOpaqueSoid
                || peer.accountSoid==0 || peer.opaqueSoid==0 || peer.smallOpaque!=-1) {
                ready=false; break;
            }
            if (peer.memberKey==record->memberKey) continue;
            auto slot=std::find_if(rows.begin(),rows.end(),[&](const Identity& row) {
                return row.memberKey==peer.memberKey;
            });
            if (slot==rows.end()) slot=std::find_if(rows.begin(),rows.end(),[](const Identity& row) {
                return row.memberKey==0;
            });
            if (slot==rows.end()) { ready=false; break; }
            if (slot->memberKey!=0 && (slot->accountSoid!=peer.accountSoid
                || slot->opaqueSoid!=peer.opaqueSoid)) { ready=false; break; }
            *slot=peer;
        }
        ready=ready && shared::private_reservations_locked(*record,rows,false);
        bool changed=false;
        for (std::size_t i=0;i<rows.size();++i)
            changed=changed||!transactions::equal(rows[i],record->membership.reservations[i]);
        if (changed && (state.stateRevision==activity::kMaximumRevision
            || record->membership.revision==kMaximumMembershipRevision)) ready=false;
        if (ready) {
            prepared.snapshot.revision+=changed?1U:0U;
            prepared.reservationsGuard=rows;
            prepared.kind=MutationKind::reservations;
            prepared.hasSnapshot=true;
            prepared.changesState=changed;
        }
    }
    ReleaseSRWLockShared(&runtime::storage::g_stateLock);
    if (ready) mutation=prepared;
    return ready;
}
}
