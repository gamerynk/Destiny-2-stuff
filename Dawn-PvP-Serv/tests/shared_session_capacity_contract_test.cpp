#include <cstdio>
#include "state/activity/transactions/internal.h"

int main() {
    using namespace dawn::state::activity;
    ActivityState state{};
    constexpr std::uint64_t owner=0x1122334455667788ULL;
    const auto check=[](bool ok,const char* name) {
        if (!ok) std::printf("FAIL: shared capacity: %s\n",name);
        return ok;
    };
    for (std::size_t i=0;i<state.sessions.size();++i) {
        auto& row=state.sessions[i];
        row.occupied=true; row.joined=true; row.ownerAccountSoid=owner;
        row.sessionId=i+1; row.createdRevision=i+2;
    }
    if (!check(transactions::select_shared_target(state,owner)==kInvalidSessionSlot,
        "full live table fails closed")) return 1;
    for (auto& row:state.sessions) row.restoredDormant=true;
    if (!check(transactions::select_shared_target(state,owner)==0,"oldest restored record")) return 1;
    state.sessions[0].restoredDormant=false;
    if (!check(transactions::select_shared_target(state,owner)==1,"rejoined record preserved")) return 1;
    state.sessions[1].sharedOwnerAccountSoid=owner;
    state.sessions[2].sharedOwnerAccountSoid=owner+1;
    state.sessions[3].ownerAccountSoid=owner+1;
    if (!check(transactions::select_shared_target(state,owner)==4,
        "canonical roots, foreign projections and wrong owners preserved")) return 1;
    state.sessions[10]={};
    if (!check(transactions::select_shared_target(state,owner)==10,"empty slot preferred")) return 1;
    state.sessions[10]=state.sessions[9];
    state.sessions[8].createdRevision=state.sessions[4].createdRevision;
    if (!check(transactions::select_shared_target(state,owner)==4,"tie uses lowest slot")) return 1;
    state.sessions[4].joined=false;
    state.sessions[4].restoredDormant=false;
    state.sessions[8].createdRevision=10;
    if (!check(transactions::select_shared_target(state,owner)==5,"pending allocation preserved")) return 1;
    std::puts("PASS: bounded shared capacity selection, live/pending/shared protection, dormant reclamation");
    return 0;
}
