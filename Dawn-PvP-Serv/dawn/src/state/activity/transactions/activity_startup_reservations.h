#pragma once
#include "../definition.h"
#include "../membership/transactions/internal.h"

namespace dawn::state::activity::transactions {
inline bool valid_startup_reservations(const PendingAllocation& plan) noexcept {
    if (!membership::transactions::equal(plan.startupOwner,plan.startupOwnerGuard)
        || (plan.startupOwner.memberKey!=0 && (plan.startupOwner.accountSoid!=plan.ownerAccountSoid
            || plan.startupOwner.smallOpaque!=-1 || plan.startupOwner.opaqueSoid==0))) return false;
    for (std::size_t i=0;i<plan.startupReservations.size();++i) {
        const auto& row=plan.startupReservations[i];
        const auto& guard=plan.startupReservationsGuard[i];
        if (row.memberKey!=guard.memberKey || row.smallOpaque!=guard.smallOpaque
            || row.signedOpaque!=guard.signedOpaque || row.joinIdentity!=guard.joinIdentity
            || row.accountSoid!=guard.accountSoid || row.opaqueSoid!=guard.opaqueSoid
            || row.secondaryOpaque!=guard.secondaryOpaque) return false;
        if (row.memberKey==0) continue;
        if (row.memberKey==membership::kInvalidOpaqueSoid || row.smallOpaque!=-1
            || row.accountSoid==0 || row.accountSoid==plan.ownerAccountSoid
            || row.opaqueSoid==0) return false;
        for (std::size_t j=0;j<i;++j)
            if (plan.startupReservations[j].memberKey==row.memberKey) return false;
    }
    return true;
}
}
