#pragma once
#include "public_region_account_context_self_test.h"
#include "state/activity/entity_retirement/runtime.h"
#include "state/activity/shared/runtime.h"

namespace restoration::tests {
inline bool private_startup_retirement_self_test() noexcept {
    using namespace dawn;
    namespace a=state::activity;
    namespace g=server::gameplay::group;
    const auto check=[](bool ok,const char* why) noexcept {
        if(!ok) std::printf("FAIL: private startup retirement: %s\n",why);
        return ok;
    };
    static state::AccountState account{};
    static a::ActivityState world{};
    account=state::account_snapshot(); account.primarySoid=0x8000840100200100ULL; world={};
    const state::AccountRuntimeContext context{&account,&world};
    const state::ScopedAccountRuntime scope(context);
    struct Cleanup { ~Cleanup(){g::reset_host_sessions();} } cleanup;
    auto destination=core::settings::get().initialActivityDefaults.defaultDestination.selection;
    constexpr char map[]="pvp_shaft";
    destination.packageName={};std::copy_n(map,sizeof(map)-1,destination.packageName.begin());
    destination.packageNameLength=sizeof(map)-1;destination.activityIndex=475;
    const a::membership::Identity identity{0xAB8401,-1,-1,0xCD8401,account.primarySoid,
        account.primarySoid+2,0};
    std::array<std::uint64_t,2> sources{}, revisions{};
    for(unsigned i=0;i<2;++i) {
        a::PendingAllocation allocation{};a::entity_slots::PendingMutation join{};
        a::membership::PendingMutation member{};
        if(!check(a::prepare_session(destination,sources[i],allocation) && a::commit(allocation)
            && a::entity_slots::prepare_join(sources[i],identity.memberKey,64,8,join)
            && a::entity_retirement::prepare_join(sources[i],account.primarySoid,identity.memberKey,
                join.mask,join.retirement),"allocate and journal fallback activity")) return false;
        revisions[i]=join.retirement.join.revision+1;
        if(!check(a::entity_slots::commit(join)
            && a::membership::prepare_identity(sources[i],identity,member)
            && a::membership::commit(member),"commit fallback join")) return false;
    }
    constexpr std::uint64_t groupId=0xB684010000000001ULL;
    bool claimed=false;
    if(!check(g::activity_host_session(groupId,0,sources[0],claimed)==0 && claimed,
        "claim initial descriptor")) return false;
    g::allocate_claimed_host_sessions();
    const auto oldHost=g::held_host_session(groupId);
    if(!check(oldHost!=0 && g::activity_host_session(groupId,0,sources[1],claimed)==0
        && g::held_host_session(groupId)==oldHost,"live source cannot be replaced")) return false;
    a::shared::disconnect(context,sources[0],revisions[0]+1);
    a::shared::disconnect(context,sources[0],0);
    if(!check(a::is_joined(sources[0]),"stale or unbound EOF cannot close a live fallback")) return false;
    a::shared::disconnect(context,sources[0],revisions[0]);
    if(!check(!a::is_joined(sources[0]) && !a::shared::has_joined_participants(
        context,account.primarySoid,sources[0]),"exact EOF retires non-shared PRIVATE source")) return false;
    if(!check(g::activity_host_session(groupId,0,sources[1],claimed)==0 && claimed
        && g::held_host_session(groupId)==0,"next launch can replace retired descriptor")) return false;
    g::allocate_claimed_host_sessions();
    const auto nextHost=g::held_host_session(groupId);
    if(!check(nextHost!=0 && nextHost!=oldHost && !a::contains(oldHost)
        && g::activity_host_session(groupId,0,sources[1],claimed)==nextHost,
        "next launch receives fresh allocated descriptor")) return false;
    a::shared::disconnect(context,sources[0],revisions[0]);
    if(!check(a::is_joined(sources[1]),"duplicate old EOF preserves new match")) return false;
    a::shared::disconnect(context,sources[1],revisions[1]);
    std::puts("PASS: fallback PRIVATE EOF retirement, stale-close guards, and repeated match startup");
    return true;
}
}
