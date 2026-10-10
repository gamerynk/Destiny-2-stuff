#pragma once
#include "public_region_account_context_self_test.h"
#include "state/activity/shared/public_admission.h"
#include "state/activity/shared/public_destination_policy.h"
#include "state/activity/transactions/internal.h"
#include <string_view>
#include <ctime>
#include "../RestorationServer/src/realtime_store.h"

namespace restoration::tests {
inline bool public_independent_arrival_case(std::uint64_t baseline,std::string_view name,
                                          std::int16_t activityIndex) noexcept {
    using namespace dawn;
    namespace activity=state::activity;
    namespace group=server::gameplay::group;
    const auto check=[](bool ok,const char* label) noexcept {
        if(!ok) std::printf("FAIL: independent public arrival: %s\n",label);
        return ok;
    };
    static std::array<state::AccountState,3> accounts{};
    static std::array<activity::ActivityState,3> worlds{};
    static std::array<PublicRegionTestSink,3> sinks{};
    static std::array<state::AccountRuntimeContext,3> contexts{};
    std::array<activity::membership::Identity,3> identities{};
    std::array<std::uint64_t,3> sources{};
    activity::destination::DestinationSelection destination{};
    if(!activity::destination::snapshot(baseline,destination)) return false;
    destination.packageName={};
    destination.packageNameLength=static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(),name.end(),destination.packageName.begin());
    destination.activityIndex=activityIndex;
    destination.descriptorBitLength=0;destination.hasDescriptorName=false;
    const auto owner=state::account_snapshot();
    for(std::size_t i=0;i<accounts.size();++i) {
        accounts[i]=owner;accounts[i].primarySoid^=std::uint64_t{0x16+i}<<20;
        worlds[i]={};sinks[i]={};
        contexts[i]={&accounts[i],&worlds[i],{}, {&sinks[i],PublicRegionTestSink::save}};
        auto& identity=identities[i];
        identity.memberKey=0xAC00000000000100ULL+i;
        identity.smallOpaque=-1;
        identity.accountSoid=accounts[i].primarySoid;
        identity.opaqueSoid=accounts[i].primarySoid+1;
        identity.joinIdentity=0xF100000000000100ULL+i;
    }
    const auto private_join=[&](std::size_t i) noexcept {
        const state::ScopedAccountRuntime scope(contexts[i]);
        activity::PendingAllocation allocation{};
        activity::entity_slots::PendingMutation join{};
        activity::membership::PendingMutation member{};
        if(!activity::prepare_session(destination,sources[i],allocation) || !activity::commit(allocation)
            || !activity::entity_slots::prepare_join(sources[i],identities[i].memberKey,64,8,join)) return false;
        join.peerConnection.hasPeer=true;join.peerConnection.hasProxy=true;
        join.peerConnection.hasFlags=true;join.peerConnection.flags=0x10;
        join.peerConnection.peer.back()=std::byte{6};
        join.peerConnection.peer[0]=std::byte{static_cast<unsigned char>(i+1)};
        join.peerConnectionGuard=join.peerConnection;
        return activity::entity_slots::commit(join)
            && activity::membership::prepare_identity(sources[i],identities[i],member)
            && activity::membership::commit(member);
    };
    struct Cleanup {~Cleanup(){group::reset_host_sessions();}} cleanup;
    for(std::size_t i=0;i<sources.size();++i)
        if(!check(private_join(i),"independent PRIVATE sources")) return false;
    constexpr std::uint64_t groupId=0x76AABBCCDD000040ULL;
    std::uint64_t host{};
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        bool claimed{};
        if(!check(group::activity_host_session(groupId,40,sources[0],claimed)==0 && claimed,"initial claim")) return false;
    }
    group::allocate_claimed_host_sessions();
    group::allocate_claimed_host_sessions();
    host=group::held_host_session(groupId);
    activity::SessionRecord root{};
    if(!check(activity::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,host,root)
        && root.publicWorld && root.sharedLeaseParts==32,"public capacity fixed before leases")) return false;
    const auto public_join=[&](std::size_t i) noexcept {
        const state::ScopedAccountRuntime scope(contexts[i]);
        activity::entity_slots::PendingMutation join{};
        activity::membership::PendingMutation member{};
        if(!activity::shared::public_join_authorized(host,identities[i].memberKey,identities[i].opaqueSoid)) return false;
        const bool prepared=i==0
            ? activity::entity_slots::prepare_join(host,identities[i].memberKey,64,8,join)
            : activity::shared::prepare_join(contexts[0],host,identities[i].memberKey,identities[i].opaqueSoid,64,8,join);
        if(!prepared || !activity::entity_slots::commit(join)) return false;
        return activity::membership::prepare_identity(host,identities[i],member) && activity::membership::commit(member);
    };
    if(!check(public_join(0),"initial owner PUBLIC join")) return false;
    const auto ownerRevision=worlds[0].stateRevision;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        bool claimed{};
        if(!check(group::activity_host_session(groupId,40,sources[1],claimed)==host && claimed,
            "node arrival selects occupied public host without private-source replacement")) return false;
        if(!check(worlds[0].stateRevision==ownerRevision,"advertisement is mutation-free")) return false;
    }
    group::allocate_claimed_host_sessions();
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        if(!check(group::source_activity_session_for_host(host)==sources[1]
            && group::region_for_host_session(host)==40,"caller-specific source and region")) return false;
    }
    {
        auto& dormant=worlds[1].sessions[5];dormant={};
        dormant.sessionId=host;dormant.ownerAccountSoid=accounts[1].primarySoid;
        dormant.occupied=true;dormant.joined=true;dormant.restoredDormant=true;
        dormant.createdRevision=dormant.recordRevision=++worlds[1].stateRevision;
        dormant.memberKey=0xBAD;
    }
    if(!check(public_join(1),"independent PUBLIC join replaces only dormant local-ID collision")) return false;
    activity::SessionRecord guest{};
    if(!check(activity::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,host,guest)
        && activity::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,host,root)
        && guest.sharedOwnerCreatedRevision==root.createdRevision && guest.sharedMemberSlot!=root.sharedMemberSlot,
        "same world, distinct member slots")) return false;
    activity::entity_slots::PendingMutation ownerGrant{}, guestGrant{};
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        if(!check(activity::entity_slots::prepare_grant(host,512,ownerGrant),"prepare elastic owner grant"))return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        if(!check(activity::entity_slots::prepare_grant(host,512,guestGrant),"prepare competing elastic guest grant"))return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        if(!check(activity::entity_slots::commit(ownerGrant),"owner borrows beyond old248 cap"))return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        if(!check(!activity::entity_slots::commit(guestGrant),"cross-account stale mask cannot overlap new owner lease"))return false;
        if(!check(activity::entity_slots::prepare_grant(host,512,guestGrant)
            && activity::entity_slots::commit(guestGrant),"fresh competing grant uses globally unheld bits"))return false;
    }
    activity::entity_slots::LeaseMask returned{};
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        activity::SessionRecord current{};
        if(!activity::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,host,current))return false;
        std::size_t picked=0;
        for(std::size_t bit=0;bit<activity::entity_slots::kSlotCount && picked<128;++bit) {
            const auto flag=std::byte{static_cast<unsigned char>(1U<<(bit%8))};
            if((current.heldEntitySlots[bit/8]&flag)!=std::byte{}) {returned[bit/8]|=flag;++picked;}
        }
        activity::entity_slots::PendingMutation release{},again{};
        if(!check(activity::entity_slots::prepare_release(host,returned,release)
            && activity::entity_slots::commit(release)
            && activity::entity_slots::prepare_grant(host,128,again)
            && again.mask==returned && activity::entity_slots::commit(again),
            "authenticated native return permits exact regrant without changing other IDs"))return false;
    }
    if(!activity::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,host,guest)
        || !activity::snapshot_for_runtime(contexts[0],accounts[0].primarySoid,host,root))return false;
    const auto memberConnection=[&](std::size_t i,core::protocol::ActivityPeerConnection& connection) noexcept {
        return activity::shared::public_member_connection(host,identities[i].memberKey,
            activity::shared::as_reservation(identities[i]).joinIdentity,connection);
    };
    {
        core::protocol::ActivityPeerConnection connection{};
        if(!check(memberConnection(0,connection) && connection.peer[0]==std::byte{1}
            && memberConnection(1,connection) && connection.peer[0]==std::byte{2},
            "native public members resolve distinct activity Steam endpoints")) return false;
        if(!check(!memberConnection(2,connection) && !connection.hasPeer,
            "unadmitted public native member has no endpoint")) return false;
        if(!check(!activity::shared::public_member_connection(host,identities[0].memberKey,
                activity::shared::as_reservation(identities[1]).joinIdentity,connection)
            && !connection.hasPeer,"wrong native player cannot borrow another member endpoint")) return false;
        auto& source=worlds[1].sessions[activity::transactions::find_session(worlds[1],sources[1])];
        const auto saved=source.membership.peerConnection;
        source.membership.peerConnection.flags=0;
        if(!check(!memberConnection(1,connection) && !connection.hasPeer,
            "native public route waits for authenticated PRIVATE readiness")) return false;
        source.membership.peerConnection=saved;
        source.membership.peerConnection.peer.back()=std::byte{0};
        if(!check(!memberConnection(1,connection) && !connection.hasPeer,
            "never advertise the server UDP address as a player Steam link")) return false;
        source.membership.peerConnection=saved;
    }
    for(std::size_t i=0;i<guest.heldEntitySlots.size();++i)
        if(!check((guest.heldEntitySlots[i]&root.heldEntitySlots[i])==std::byte{0},"disjoint entity leases")) return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        activity::membership::PendingMutation refresh{};
        activity::shared::Projection view{};
        if(!check(activity::membership::prepare_refresh(host,0,-1,refresh)
            && activity::shared::project(host,refresh.snapshot,view,sources[0])
            && (view.joinedMemberMask&3)==3
            && view.members[1].memberKey==identities[1].memberKey
            && view.peerConnections[1].hasPeer && view.peerConnections[1].peer[0]==std::byte{2},
            "public projection uses independent PRIVATE peer address")) return false;
    }
    if(!check(activity::shared::public_peers(accounts[0].primarySoid,accounts[1].primarySoid)
        && !activity::shared::public_peers(accounts[0].primarySoid,accounts[2].primarySoid),
        "public-only signaling authorization denies unrelated account")) return false;
    namespace realtime=middleware::social::realtime;
    constexpr std::array<std::uint64_t,3> platforms{0x01100001FFF00001ULL,0x01100001FFF00002ULL,0x01100001FFF00003ULL};
    const auto now=static_cast<std::uint64_t>(std::time(nullptr));
    std::array<std::uint64_t,3> lobbies{};
    for(std::size_t i=0;i<platforms.size();++i) {
        realtime::Request request{};realtime::Response response{};
        request.operation=realtime::Operation::createLobby;request.valueA=2;request.valueB=3;
        if(!check(realtime_store::begin_game_session(platforms[i],accounts[i].primarySoid,1,now)
            && realtime_store::mark_gameplay_ready(platforms[i],accounts[i].primarySoid,1,now)
            && realtime_store::execute(platforms[i],accounts[i].primarySoid,now,request,nullptr,response)
            && response.result==1 && response.lobby.memberCount==1,"separate authenticated fireteam lobbies")) return false;
        lobbies[i]=response.lobby.id;
    }
    const auto signal=[&](std::size_t from,std::size_t to,int result) noexcept {
        realtime::Request request{};realtime::Response response{};
        request.operation=realtime::Operation::sendRendezvous;request.lobbyId=lobbies[from];
        request.targetPlatformId=platforms[to];request.payloadSize=8;request.payload[4]=std::byte{0x7A};
        return realtime_store::execute(platforms[from],accounts[from].primarySoid,now,request,nullptr,response)
            && response.result==result && response.lobby.id==lobbies[from] && response.lobby.memberCount==1;
    };
    if(!check(lobbies[0]!=lobbies[1] && signal(0,1,1) && signal(1,0,1) && signal(0,2,15),
        "real rendezvous store routes both ways across separate lobbies, denies outsider")) return false;
    {
        realtime::Request request{};realtime::Response response{};
        if(!check(realtime_store::execute(platforms[1],accounts[1].primarySoid,now,request,nullptr,response),
            "receiver polls rendezvous")) return false;
        const bool received=std::any_of(response.events.begin(),response.events.begin()+response.eventCount,
            [&](const auto& event){return event.kind==realtime::EventKind::rendezvous
                && event.sourcePlatformId==platforms[0] && event.payloadSize==8 && event.payload[4]==std::byte{0x7A};});
        if(!check(received && response.lobby.id==lobbies[1] && response.lobby.memberCount==1,
            "exact packet delivered without merging receiver fireteam")) return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[2]);
        if(!check(!activity::shared::public_join_authorized(host,identities[1].memberKey,identities[1].opaqueSoid),
            "foreign account cannot borrow arrival")) return false;
    }
    if(!check(activity::shared::disconnect_member(host,identities[1].memberKey)
        && !activity::shared::public_peers(accounts[0].primarySoid,accounts[1].primarySoid),
        "departure immediately withdraws signaling authorization")) return false;
    if(!check(signal(0,1,15) && signal(1,0,15),"real rendezvous permission withdrawn both ways after departure")) return false;
    if(!check(private_join(1),"fresh PRIVATE after departure")) return false;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);bool claimed{};
        if(!check(group::activity_host_session(groupId,40,sources[1],claimed)==host && claimed,"fresh node return selects survivor")) return false;
    }
    group::allocate_claimed_host_sessions();
    const auto departedLease=guest.heldEntitySlots;
    if(!check(public_join(1),"first fresh node return rejoins PUBLIC without restart")) return false;
    activity::SessionRecord returnedGuest{};
    if(!activity::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,host,returnedGuest))return false;
    for(std::size_t i=0;i<departedLease.size();++i)
        if(!check((returnedGuest.heldEntitySlots[i]&departedLease[i])==std::byte{},
            "departure and rejoin never reuse unreturned native IDs"))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        if(!check(activity::release_session(sources[1]),"PUBLIC outlives its PRIVATE transport")) return false;
    }
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        activity::membership::PendingMutation refresh{};
        activity::shared::Projection view{};
        if(!check(activity::membership::prepare_refresh(host,0,-1,refresh)
            && activity::shared::project(host,refresh.snapshot,view,sources[0])
            && (view.joinedMemberMask&3)==3 && view.peerConnections[1].hasPeer
            && view.peerConnections[1].peer[0]==std::byte{2},
            "exact admitted endpoint survives PRIVATE closure while PUBLIC remains")) return false;
        core::protocol::ActivityPeerConnection connection{};
        if(!check(memberConnection(1,connection) && connection.peer==view.peerConnections[1].peer,
            "native managed route retains exact activity address after PRIVATE closure")) return false;
    }
    if(!check(activity::shared::disconnect_member(host,identities[0].memberKey),"original allocator leaves")) return false;
    if(!check(activity::snapshot_for_runtime(contexts[1],accounts[1].primarySoid,host,guest)
        && guest.joined,"independent visitor survives allocator departure")) return false;
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        activity::SessionRecord retained{};
        if(!check(activity::release_session(sources[0]) && group::source_activity_snapshot(groupId,retained)
            && retained.publicWorld && retained.destination.activityIndex==activityIndex,
            "world descriptor outlives the original PRIVATE source")) return false;
    }
    {
        core::protocol::ActivityPeerConnection connection{};
        if(!check(activity::shared::disconnect_member(host,identities[1].memberKey)
            && !memberConnection(1,connection) && !connection.hasPeer,
            "retained native endpoint expires with departed PUBLIC incarnation")) return false;
    }
    auto mission=destination;++mission.activityIndex;
    if(!check(!activity::shared::compatible_public_destination(destination,mission),"different activity never merges")) return false;
    mission=destination;mission.packageName[0]='x';
    if(!check(!activity::shared::compatible_public_destination(destination,mission),"different package never merges")) return false;
    std::printf("PASS: independent public node arrivals, source rebinding, disjoint leases and signaling isolation: %.*s\n",
        static_cast<int>(name.size()),name.data());
    return true;
}
inline bool public_independent_arrival_self_test(std::uint64_t baseline) noexcept {
    std::int16_t fixtureIndex=100;
    for(const auto name:dawn::state::activity::shared::kPublicDestinationPackages)
        if(!public_independent_arrival_case(baseline,name,fixtureIndex++)) return false;
    return true;
}
}
