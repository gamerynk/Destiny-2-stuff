#pragma once
#include "public_independent_arrival_self_test.h"

namespace restoration::tests {
inline bool public_party_arrival_case(std::uint64_t baseline,std::string_view name,bool visitorFirst) noexcept {
    using namespace dawn;
    namespace a=state::activity;
    namespace g=server::gameplay::group;
    const auto check=[](bool value,const char* step){if(!value)std::printf("FAIL: public party arrival: %s\n",step);return value;};
    static std::array<state::AccountState,3> accounts{};
    static std::array<a::ActivityState,3> worlds{};
    static std::array<PublicRegionTestSink,3> sinks{};
    static std::array<state::AccountRuntimeContext,3> contexts{};
    std::array<a::membership::Identity,3> identities{};
    a::destination::DestinationSelection destination{};
    if(!a::destination::snapshot(baseline,destination))return false;
    destination.packageName={};destination.packageNameLength=static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(),name.end(),destination.packageName.begin());
    destination.activityIndex=200;destination.hasDescriptorName=false;destination.descriptorBitLength=0;
    const auto configured=state::account_snapshot();
    for(std::size_t i=0;i<3;++i){
        accounts[i]=configured;accounts[i].primarySoid^=std::uint64_t{0x20+i}<<20;
        worlds[i]={};sinks[i]={};contexts[i]={&accounts[i],&worlds[i],{}, {&sinks[i],PublicRegionTestSink::save}};
        auto& id=identities[i];id.memberKey=0xAD00000000000100ULL+i;id.smallOpaque=-1;
        id.accountSoid=accounts[i].primarySoid;id.opaqueSoid=id.accountSoid+1;id.joinIdentity=0xF200000000000100ULL+i;
    }
    struct Cleanup{~Cleanup(){g::reset_host_sessions();}}cleanup;
    std::array<std::uint64_t,3> sources{};
    const auto finishJoin=[&](std::size_t i,a::entity_slots::PendingMutation& plan){
        plan.peerConnection.hasPeer=plan.peerConnection.hasProxy=plan.peerConnection.hasFlags=true;
        plan.peerConnection.flags=0x10;plan.peerConnection.peer[0]=std::byte{static_cast<unsigned char>(i+1)};
        plan.peerConnection.peer.back()=std::byte{6};plan.peerConnectionGuard=plan.peerConnection;
        a::membership::PendingMutation identity{};
        return a::entity_slots::commit(plan)
            && a::membership::prepare_identity(sources[i],identities[i],identity) && a::membership::commit(identity);
    };
    for(const auto i:std::array<std::size_t,2>{0,2}){
        const state::ScopedAccountRuntime scope(contexts[i]);
        a::PendingAllocation allocation{};a::entity_slots::PendingMutation join{};
        if(!check(a::prepare_session(destination,sources[i],allocation)
            && (i==2 || a::prepare_startup_reservations(allocation,std::span(identities).first(2)))
            && a::commit(allocation) && a::entity_slots::prepare_join(sources[i],identities[i].memberKey,64,8,join)
            && finishJoin(i,join),"party leader / independent visitor PRIVATE"))return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[1]);sources[1]=sources[0];
        a::entity_slots::PendingMutation join{};
        if(!check(a::shared::prepare_join(contexts[0],sources[0],identities[1].memberKey,
            identities[1].opaqueSoid,64,8,join) && finishJoin(1,join),"follower uses party PRIVATE, not a fake solo source"))return false;
    }
    constexpr std::uint64_t groupId=0x77AABBCCDD000040ULL;
    const std::size_t owner=visitorFirst?2:0;
    {
        const state::ScopedAccountRuntime scope(contexts[owner]);bool claimed{};
        if(!check(g::activity_host_session(groupId,48,sources[owner],claimed)==0 && claimed,"first public claim"))return false;
    }
    auto& initial=worlds[owner].sessions[a::transactions::find_session(worlds[owner],sources[owner])];
    initial.membership.hasIdentity=false;
    const auto before=worlds[owner].stateRevision;
    g::allocate_claimed_host_sessions();
    if(!check(g::held_host_session(groupId)==0 && worlds[owner].stateRevision==before,
        "incomplete identity waits without allocating a permanently private public host"))return false;
    initial.membership.hasIdentity=true;
    g::allocate_claimed_host_sessions();g::allocate_claimed_host_sessions();
    const auto host=g::held_host_session(groupId);
    a::SessionRecord root{};
    if(!check(host!=0 && a::snapshot_for_runtime(contexts[owner],accounts[owner].primarySoid,host,root)
        && root.publicWorld && root.sharedLeaseParts==32,"public allocation independent of party size/order"))return false;
    const std::array<std::size_t,3> order=visitorFirst?std::array<std::size_t,3>{2,0,1}:std::array<std::size_t,3>{0,1,2};
    for(const auto i:order){
        const state::ScopedAccountRuntime scope(contexts[i]);bool claimed{};
        if(!check(g::activity_host_session(groupId,48,sources[i],claimed)==host && claimed,
            "party member and unrelated visitor resolve identical public world"))return false;
        g::allocate_claimed_host_sessions();
        if(!check(a::shared::public_join_authorized(host,identities[i].memberKey,identities[i].opaqueSoid),"individual public admission"))return false;
        a::entity_slots::PendingMutation join{};a::membership::PendingMutation identity{};
        const bool prepared=i==owner?a::entity_slots::prepare_join(host,identities[i].memberKey,64,8,join)
            :a::shared::prepare_join(contexts[owner],host,identities[i].memberKey,identities[i].opaqueSoid,64,8,join);
        if(!check(prepared && a::entity_slots::commit(join)
            && a::membership::prepare_identity(host,identities[i],identity) && a::membership::commit(identity),"three PUBLIC joins"))return false;
    }
    std::array<a::SessionRecord,3> publicRows{};
    for(std::size_t i=0;i<3;++i){
        core::protocol::ActivityPeerConnection connection{};
        const bool resolved=a::shared::public_member_connection(host,identities[i].memberKey,
            a::shared::as_reservation(identities[i]).joinIdentity,connection);
        if(!check(a::snapshot_for_runtime(contexts[i],accounts[i].primarySoid,host,publicRows[i])
            && publicRows[i].joined && publicRows[i].sharedOwnerCreatedRevision==root.createdRevision
            && resolved && connection.peer[0]==std::byte{static_cast<unsigned char>(i+1)},"three distinct admitted peer addresses")){
            std::printf("fixture owner=%zu member=%zu host=%llX source=%llX joined=%u created=%llu expected=%llu resolved=%u peer=%u\n",
                owner,i,static_cast<unsigned long long>(host),static_cast<unsigned long long>(sources[i]),publicRows[i].joined?1U:0U,
                static_cast<unsigned long long>(publicRows[i].sharedOwnerCreatedRevision),static_cast<unsigned long long>(root.createdRevision),
                resolved?1U:0U,std::to_integer<unsigned>(connection.peer[0]));return false;
        }
        for(std::size_t j=0;j<i;++j)for(std::size_t k=0;k<publicRows[i].heldEntitySlots.size();++k)
            if(!check((publicRows[i].heldEntitySlots[k]&publicRows[j].heldEntitySlots[k])==std::byte{0},"disjoint public leases"))return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[owner]);
        a::membership::PendingMutation refresh{};a::shared::Projection view{};
        if(!check(a::membership::prepare_refresh(host,0,-1,refresh)
            && a::shared::project(host,refresh.snapshot,view,sources[owner]) && view.joinedMemberMask==7,
            "party and visitor present in same world projection"))return false;
    }
    namespace realtime=middleware::social::realtime;
    constexpr std::array<std::uint64_t,3> platforms{0x01100001FFE00001ULL,0x01100001FFE00002ULL,0x01100001FFE00003ULL};
    const auto now=static_cast<std::uint64_t>(std::time(nullptr));
    for(std::size_t i=0;i<3;++i)if(!check(realtime_store::begin_game_session(platforms[i],accounts[i].primarySoid,1,now)
        && realtime_store::mark_gameplay_ready(platforms[i],accounts[i].primarySoid,1,now),"authenticated lobby fixture"))return false;
    std::array<std::uint64_t,3> lobbies{};
    const auto partyFriends=+[](std::uint64_t first,std::uint64_t second) noexcept {
        return (first==0x01100001FFE00001ULL && second==0x01100001FFE00002ULL)
            || (first==0x01100001FFE00002ULL && second==0x01100001FFE00001ULL);
    };
    for(const auto i:std::array<std::size_t,3>{0,2,1}){
        realtime::Request request{};realtime::Response response{};
        request.operation=i==1?realtime::Operation::joinLobby:realtime::Operation::createLobby;
        request.valueA=2;request.valueB=3;if(i==1)request.lobbyId=lobbies[0];
        if(!check(realtime_store::execute(platforms[i],accounts[i].primarySoid,now,request,partyFriends,response)
            && response.result==1,"actual party/visitor lobby operations"))return false;
        lobbies[i]=response.lobby.id;
    }
    for(std::size_t from=0;from<3;++from)for(std::size_t to=0;to<3;++to){
        if(from==to)continue;
        realtime::Request request{};realtime::Response response{};
        request.operation=realtime::Operation::sendRendezvous;request.lobbyId=lobbies[from];
        request.targetPlatformId=platforms[to];request.payloadSize=8;
        if(!check(realtime_store::execute(platforms[from],accounts[from].primarySoid,now,request,nullptr,response)
            && response.result==1 && response.lobby.id==lobbies[from]
            && response.lobby.memberCount==(from==2?1:2),"all directions connect without merging fireteams"))return false;
    }
    if(!check(lobbies[0]==lobbies[1] && lobbies[0]!=lobbies[2],"private party identity preserved"))return false;
    if(!check(a::shared::disconnect_member(host,identities[0].memberKey)
        && a::snapshot_for_runtime(contexts[2],accounts[2].primarySoid,host,root) && root.joined,
        "public visitor survives party leader departure"))return false;
    std::printf("PASS: mixed public party + visitor, six signaling directions, identity wait, %.*s visitor_first=%u\n",
        static_cast<int>(name.size()),name.data(),visitorFirst?1U:0U);
    return true;
}
inline bool public_party_arrival_self_test(std::uint64_t baseline) noexcept {
    for(const auto name:{std::string_view{"edz_freeroam"},std::string_view{"city_tower_social_d2"},std::string_view{"campaign_social_space_d2"}})
        for(const bool visitorFirst:{false,true})if(!public_party_arrival_case(baseline,name,visitorFirst))return false;
    return true;
}
}
