#pragma once
#include "private_startup_retirement_self_test.h"
#include "server/gameplay/gameplay_advertisement.h"
#include "server/gameplay/endpoint/gameplay_endpoint.h"

namespace restoration::tests {
inline bool concurrent_private_regions_self_test() noexcept {
    using namespace dawn;
    namespace a=state::activity;
    namespace g=server::gameplay::group;
    namespace gameplay=server::gameplay;
    const auto check=[](bool ok,const char* why) noexcept {
        if(!ok) std::printf("FAIL: concurrent private regions: %s\n",why);
        return ok;
    };
    static std::array<state::AccountState,4> accounts{};
    static std::array<a::ActivityState,4> worlds{};
    std::array<state::AccountRuntimeContext,4> contexts{};
    std::array<a::membership::Identity,4> members{};
    for(unsigned i=0;i<4;++i) {
        accounts[i]=state::account_snapshot();
        accounts[i].primarySoid=0x8000860100100100ULL+std::uint64_t{i}*0x100;
        worlds[i]={};contexts[i]={&accounts[i],&worlds[i]};
        members[i]={0xAB8600+i,-1,-1,0xCD8600+i,accounts[i].primarySoid,
                    accounts[i].primarySoid+2,0xEF8600+i};
    }
    auto destination=core::settings::get().initialActivityDefaults.defaultDestination.selection;
    destination.packageName={};constexpr char map[]="pvp_shaft";
    std::copy_n(map,sizeof(map)-1,destination.packageName.begin());
    destination.packageNameLength=sizeof(map)-1;destination.activityIndex=475;
    const gameplay::endpoint::Identity base{0x2244668800112233ULL,0x1122334455667788ULL};
    std::array<std::uint64_t,2> sources{},hosts{};
    std::array<gameplay::endpoint::Identity,2> regions{};
    struct Cleanup {
        std::array<state::AccountRuntimeContext,4>& contexts;
        std::array<std::uint64_t,2>& sources;
        ~Cleanup() {
            g::reset_host_sessions();
            for(unsigned i=0;i<4;++i) if(sources[i/2])
                a::shared::disconnect(contexts[i],sources[i/2]);
        }
    } cleanup{contexts,sources};
    for(unsigned match=0;match<2;++match) {
        const unsigned owner=match*2;
        {
            const state::ScopedAccountRuntime scope(contexts[owner]);
            a::PendingAllocation allocation{};a::entity_slots::PendingMutation join{};
            if(!check(a::prepare_session(destination,sources[match],allocation)
                && a::prepare_startup_reservations(allocation,std::span(members).subspan(owner,2))
                && a::commit(allocation)
                && a::entity_slots::prepare_join(sources[match],members[owner].memberKey,64,8,join)
                && a::entity_slots::commit(join),"allocate two-player Javelin match")) return false;
        }
        {
            const state::ScopedAccountRuntime scope(contexts[owner+1]);
            a::entity_slots::PendingMutation join{};
            if(!check(a::shared::prepare_join(contexts[owner],sources[match],members[owner+1].memberKey,
                members[owner+1].opaqueSoid,64,8,join) && a::entity_slots::commit(join),
                "join each guest to its own lobby")) return false;
        }
        for(unsigned peer=owner;peer<owner+2;++peer) {
            const state::ScopedAccountRuntime scope(contexts[peer]);
            a::membership::PendingMutation member{};
            if(!check(a::membership::prepare_identity(sources[match],members[peer],member)
                && a::membership::commit(member),"commit authenticated player identity")) return false;
        }
        const state::ScopedAccountRuntime scope(contexts[owner]);
        if(!check(gameplay::region_descriptor_identity(sources[match],0,base,regions[match]),
            "derive real advertisement identity")) return false;
        bool claimed{};
        if(!check(g::activity_host_session(regions[match].machineId,0,sources[match],claimed)==0
            && claimed,"claim independent region host")) return false;
        g::allocate_claimed_host_sessions();
        hosts[match]=g::held_host_session(regions[match].machineId);
        if(!check(hosts[match]!=0,"allocate independent region host")) return false;
        if(match==1 && !check(regions[0].machineId!=regions[1].machineId
            && regions[0].onlineSessionId!=regions[1].onlineSessionId,
            "two live Javelin region-zero matches must not alias")) return false;
    }
    if(!check(sources[0]==sources[1],"fixture exercises colliding account-local session IDs")) return false;
    for(unsigned round=0;round<16;++round) for(unsigned peer=0;peer<4;++peer) {
        const unsigned match=peer/2;
        const state::ScopedAccountRuntime scope(contexts[peer]);
        gameplay::endpoint::Identity id{};
        if(!check(gameplay::region_descriptor_identity(sources[match],0,base,id)
            && id.machineId==regions[match].machineId
            && id.onlineSessionId==regions[match].onlineSessionId,
            "owner and guest retain one descriptor for their match")) return false;
        bool claimed{};
        if(!check(g::activity_host_session(id.machineId,0,sources[match],claimed)==hosts[match]
            && claimed,"alternating lobbies keep both hosts ready")) return false;
    }
    for(unsigned match=0;match<2;++match) {
        const state::ScopedAccountRuntime scope(contexts[match*2]);
        gameplay::endpoint::Identity id{};
        if(!check(gameplay::region_descriptor_identity(hosts[match],0,base,id)
            && id.machineId==regions[match].machineId,
            "host-session keepalive resolves original private scope")) return false;
        if(!check(gameplay::region_descriptor_identity(sources[match],1,base,id)
            && id.machineId!=regions[match].machineId,"regions within a match stay distinct")) return false;
    }
    a::shared::disconnect(contexts[0],sources[0]);
    a::shared::disconnect(contexts[1],sources[0]);
    {
        const state::ScopedAccountRuntime scope(contexts[2]);
        bool claimed{};
        if(!check(a::is_joined(sources[1])
            && g::activity_host_session(regions[1].machineId,0,sources[1],claimed)==hosts[1],
            "closing first match preserves second match")) return false;
    }
    auto publicDestination=destination;
    publicDestination.packageName={};constexpr char patrol[]="edz_freeroam";
    std::copy_n(patrol,sizeof(patrol)-1,publicDestination.packageName.begin());
    publicDestination.packageNameLength=sizeof(patrol)-1;publicDestination.activityIndex=8;
    gameplay::endpoint::Identity publicIdentity{};
    for(unsigned peer=0;peer<2;++peer) {
        const state::ScopedAccountRuntime scope(contexts[peer]);
        a::PendingAllocation allocation{};std::uint64_t session{};
        gameplay::endpoint::Identity id{};
        if(!check(a::prepare_session(publicDestination,session,allocation) && a::commit(allocation)
            && gameplay::region_descriptor_identity(session,0,base,id),"public arrival identity")) return false;
        if(peer==0) publicIdentity=id;
        if(!check(id.machineId==publicIdentity.machineId
            && id.onlineSessionId==publicIdentity.onlineSessionId
            && id.machineId==(base.machineId^0x9E3779B97F4A7C15ULL),
            "public shared-region identity remains unchanged")) return false;
        id=base;
        if(!check(!gameplay::region_descriptor_identity(0,0,base,id)
            && id.machineId==0 && id.onlineSessionId==0,"unknown source fails closed")) return false;
        (void)a::release_session(session);
    }
    std::puts("PASS: concurrent Javelin matches, colliding local IDs, canonical guests, host keepalives, departure isolation");
    return true;
}
}
