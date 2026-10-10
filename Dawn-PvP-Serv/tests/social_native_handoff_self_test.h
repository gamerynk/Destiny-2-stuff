#pragma once
#include "steam/runtime/social/lobby_cache.h"
#include "social_search_self_test.h"
#include "../RestorationServer/src/realtime_store.h"
#include "middleware/gameplay/descriptor/join_descriptor.h"

namespace restoration::tests {
inline bool social_native_handoff_self_test(const dawn::state::AccountRuntimeContext& target,
    const dawn::state::AccountRuntimeContext& caller,std::uint64_t world,
    std::uint32_t activityHash) noexcept {
    using namespace dawn;
    namespace rt=middleware::social::realtime;namespace mm=state::matchmaking;namespace a=state::activity;
    const auto check=[](bool ok,const char* label){if(!ok)std::printf("FAIL: social native handoff: %s\n",label);return ok;};
    constexpr std::array<std::uint64_t,3> platforms{0x01100001000000D1ULL,0x01100001000000D2ULL,0x01100001000000D3ULL};
    const std::array<std::uint64_t,3> accounts{target.account->primarySoid,caller.account->primarySoid,0x9EAA3000FF0000D3ULL};
    const auto friends=+[](std::uint64_t,std::uint64_t) noexcept{return true;};
    const auto noFriends=+[](std::uint64_t,std::uint64_t) noexcept{return false;};
    const auto now=static_cast<std::uint64_t>(std::time(nullptr));
    realtime_store::shutdown();
    for(std::size_t i=0;i<platforms.size();++i)
        if(!check(realtime_store::begin_game_session(platforms[i],accounts[i],1,now)
            && realtime_store::mark_gameplay_ready(platforms[i],accounts[i],1,now),"authenticated fixture"))return false;
    rt::Request request{};static rt::Response response{};std::array<std::uint64_t,3> cursors{};
    const auto run=[&](std::size_t i,bool allowFriends=true) {
        request.cursor=cursors[i];
        const bool ok=realtime_store::execute(platforms[i],accounts[i],now,request,
            allowFriends?friends:noFriends,response);cursors[i]=response.cursor;
        return ok && response.result==1;
    };
    const auto create=[&](std::size_t i){request={};request.operation=rt::Operation::createLobby;
        request.valueA=3;request.valueB=6;return run(i);};
    const auto leave=[&](std::size_t i,std::uint64_t lobby){request={};request.operation=rt::Operation::leaveLobby;
        request.lobbyId=lobby;return run(i);};
    const auto join=[&](std::size_t i,std::uint64_t lobby,bool allowFriends=true){request={};request.operation=rt::Operation::joinLobby;
        request.lobbyId=lobby;return run(i,allowFriends);};
    const auto publish=[&](std::size_t i,std::uint64_t lobby){
        middleware::gameplay::descriptor::JoinEndpoint endpoint{};
        endpoint.machineId=platforms[i]^0xAB00000000000000ULL;endpoint.address=0xCB00712A;
        endpoint.port=27016;endpoint.onlineSessionId=lobby;realtime_store::GameplayBootstrapDescriptor bytes{};
        if(!middleware::gameplay::descriptor::build(endpoint,bytes))return false;
        request={};request.operation=rt::Operation::publishNativeFireteamDescriptor;request.lobbyId=lobby;
        request.payloadSize=bytes.size();std::copy(bytes.begin(),bytes.end(),request.payload.begin());return run(i);
    };
    request={};request.operation=rt::Operation::createLobby;request.valueA=1;request.valueB=6;
    if(!check(run(1),"caller regular lobby for exact three-slot cache"))return false;
    const auto callerRegular=response.lobby;
    if(!check(create(0),"host fireteam"))return false;
    const auto targetFireteam=response.lobby.id;
    if(!check(publish(0,targetFireteam) && create(0),"host Tower native session"))return false;
    const auto targetActivity=response.lobby.id;
    if(!check(create(1),"caller original fireteam"))return false;
    const auto originalFireteam=response.lobby.id;
    if(!check(publish(1,originalFireteam) && create(1) && join(1,targetFireteam) && join(1,targetActivity),
        "initial shared Tower fireteam"))return false;
    if(!check(create(1),"native Leave Fireteam creates solo before old handles leave"))return false;
    const auto soloFireteam=response.lobby.id;
    if(!check(publish(1,soloFireteam) && !leave(1,targetFireteam) && leave(1,targetActivity) && create(1),
        "Orbit preserves solo g0; Tower node creates temporary activity"))return false;
    auto temporaryActivity=response.lobby.id;
    rt::Lobby lobby{};
    const auto intact=[&]{return realtime_store::lobby_snapshot(soloFireteam,lobby) && lobby.memberCount==1
        && realtime_store::lobby_snapshot(temporaryActivity,lobby) && lobby.memberCount>=1;};
    if(!check(!join(1,targetActivity) && response.result==25 && intact(),
        "unmatched public target cannot retire existing sessions"))return false;
    std::array<mm::ContextHandle,2> handles{};std::array<std::array<std::byte,128>,2> descriptors{};
    for(std::size_t i=0;i<handles.size();++i) {
        if(!mm::acquire_context(handles[i]))return false;
        auto& d=descriptors[i];const auto machine=std::uint64_t{0xAD1000}+i;
        const auto session=i==0?targetActivity:temporaryActivity;
        std::memcpy(d.data(),&machine,8);std::memcpy(d.data()+110,&session,8);
        const auto text=std::string("steamid:")+std::to_string(platforms[i])+"#11223344";
        std::memcpy(d.data()+8,text.data(),text.size());d[93]=std::byte{6};d[94]=std::byte{0xA1};
    }
    a::SessionRecord old{};
    if(!a::snapshot_for_runtime(target,accounts[0],world,old))return false;
    {const state::ScopedAccountRuntime scope(target);a::membership::AuthoritativeUpdate update{};a::membership::PendingMutation plan{};
        update.peerConnection=old.membership.peerConnection;
        std::copy_n(descriptors[0].begin()+8,86,update.peerConnection.peer.begin());
        if(!a::membership::prepare_authoritative(world,update,plan) || !a::membership::commit(plan))return false;}
    const auto advertise=[&](std::size_t i){const state::ScopedAccountRuntime scope(i==0?target:caller);
        mm::PendingMutation mutation{};std::uint64_t id{};
        if(!mm::prepare_variant_update(handles[i],0,3,true,descriptors[i],id,mutation))return false;
        mutation.socialAccount=accounts[i];mutation.socialPlatform=platforms[i];mutation.socialActivity=activityHash;
        mutation.socialPrimary=true;mutation.socialParty[0]=accounts[i];return mm::commit(mutation);};
    const auto search=[&]{const state::ScopedAccountRuntime scope(caller);mm::LatestSnapshot result{};mm::PendingMutation grant{};
        return mm::prepare_social_search(handles[1],activityHash,result,grant)
            && result.descriptor==descriptors[0] && mm::commit(grant);};
    if(!check(advertise(0) && advertise(1) && search() && mm::social_search_peers(accounts[1],accounts[0],world),
        "real committed matchmaking result for exact live Tower"))return false;
    if(!check(mm::social_search_native_lobby(accounts[1],accounts[0],platforms[0],targetActivity)
        && !mm::social_search_native_lobby(accounts[1],accounts[0],platforms[0],targetFireteam)
        && !mm::social_search_native_lobby(accounts[1],accounts[0],platforms[1],targetActivity)
        && !mm::social_search_native_lobby(accounts[0],accounts[1],platforms[1],temporaryActivity)
        && !mm::social_search_native_lobby(accounts[2],accounts[0],platforms[0],targetActivity),
        "exact native lobby/owner/direction/party checked"))return false;
    request={};request.operation=rt::Operation::setMemberLimit;request.lobbyId=targetActivity;request.valueA=1;
    if(!check(run(0) && !join(1,targetActivity) && response.result==25 && intact(),"full target preserves source"))return false;
    request.valueA=6;request.operation=rt::Operation::setMemberLimit;request.lobbyId=targetActivity;
    if(!run(0))return false;
    request={};request.operation=rt::Operation::setJoinable;request.lobbyId=targetActivity;
    if(!check(run(0) && !join(1,targetActivity) && response.result==15 && intact(),"closed target preserves source"))return false;
    request={};request.operation=rt::Operation::setJoinable;request.lobbyId=targetActivity;request.valueA=1;
    if(!run(0))return false;
    if(!check(join(2,temporaryActivity) && !join(1,targetActivity) && response.result==25 && intact(),
        "never destroy a temporary activity with another member"))return false;
    if(!leave(2,temporaryActivity))return false;
    descriptors[0][94]^=std::byte{1};if(!advertise(0))return false;
    if(!check(!join(1,targetActivity) && response.result==25 && intact(),"rekey revokes old admission proof"))return false;
    if(!check(search(),"new exact search grant"))return false;
    namespace lc=steam::runtime::social::lobby_cache;
    static lc::Cache clientCache;clientCache={};clientCache.remember(callerRegular);
    rt::Lobby soloSnapshot{},temporarySnapshot{};
    if(!check(realtime_store::lobby_snapshot(soloFireteam,soloSnapshot)
        && realtime_store::lobby_snapshot(temporaryActivity,temporarySnapshot),"pre-return client cache inputs"))return false;
    clientCache.remember(soloSnapshot);clientCache.remember(temporarySnapshot);
    if(!check(join(1,targetActivity),"matched solo Tower return replaces only temporary activity before native leave"))return false;
    lc::store_response(clientCache,response,&soloSnapshot,platforms[1]);
    if(!check(clientCache.snapshot(targetActivity).ownerPlatformId==platforms[0]
        && clientCache.snapshot(targetActivity).memberCount==2
        && clientCache.snapshot(temporaryActivity).id==0 && clientCache.owned(soloFireteam,platforms[1])
        && clientCache.snapshot(callerRegular.id).id==callerRegular.id,
        "RUN88 exact native return response cached before LobbyEnter owner/address queries"))return false;
    if(!check(!realtime_store::lobby_snapshot(temporaryActivity,lobby)
        && realtime_store::lobby_snapshot(soloFireteam,lobby) && lobby.memberCount==1 && lobby.ownerPlatformId==platforms[1]
        && realtime_store::lobby_snapshot(targetFireteam,lobby) && lobby.memberCount==1 && lobby.ownerPlatformId==platforms[0]
        && realtime_store::lobby_snapshot(targetActivity,lobby) && lobby.memberCount==2
        && realtime_store::lobby_for_member(platforms[1],lobby) && lobby.id==soloFireteam,
        "native activity shared, independent fireteams remain separate"))return false;
    bool left=false,entered=false;
    for(std::size_t i=0;i<response.eventCount;++i){const auto& event=response.events[i];std::uint32_t flags{};
        if(event.kind!=rt::EventKind::memberChanged || event.memberPlatformId!=platforms[1] || event.payloadSize!=4)continue;
        std::memcpy(&flags,event.payload.data(),4);
        left|=event.lobbyId==temporaryActivity && flags==2;entered|=event.lobbyId==targetActivity && flags==1;}
    if(!check(left && entered,"exact old LEFT/new ENTERED callbacks"))return false;
    if(!check(!leave(1,temporaryActivity) && join(1,targetActivity) && response.lobby.memberCount==2
        && !create(1) && response.result==25,"delayed leave/idempotent join and two-session limit"))return false;
    if(!check(leave(1,targetActivity) && create(1),"second independent node departure"))return false;
    temporaryActivity=response.lobby.id;
    if(!check(!join(1,targetFireteam,false) && response.result==15 && intact(),
        "public search cannot authorize private fireteam"))return false;
    clientCache.forget(targetActivity);clientCache.remember(response.lobby);
    if(!realtime_store::lobby_snapshot(temporaryActivity,temporarySnapshot))return false;
    clientCache.remember(temporarySnapshot);
    for(unsigned i=0;i<4;++i){request={};request.operation=rt::Operation::invite;
        request.lobbyId=targetFireteam;request.targetPlatformId=platforms[1];if(!run(0))return false;}
    if(!check(join(1,targetActivity,false) && response.lobby.memberCount==2,
        "exact public matchmaking admits strangers without merging fireteams"))return false;
    const auto accepted=response;
    lc::retire_response_departures(clientCache,accepted,&soloSnapshot,platforms[1]);
    if(!check(!clientCache.can_remember(accepted.lobby),"RUN88 older event page delays temporary LEFT"))return false;
    unsigned queries=0;
    for(const auto id:clientCache.retained_ids()){
        if(!id||id==accepted.lobby.id||id==soloSnapshot.id)continue;
        rt::Request query{};rt::Response reply{};query.operation=rt::Operation::queryLobby;
        query.lobbyId=id;query.cursor=accepted.cursor;++queries;
        if(!realtime_store::execute(platforms[1],accounts[1],now,query,noFriends,reply))return false;
        if(lc::authoritative_absence(query,reply))clientCache.forget(id);
        if(!check(reply.cursor==accepted.cursor && reply.eventCount==0,"membership query never consumes queued callbacks"))return false;
        if(clientCache.can_remember(accepted.lobby))break;
    }
    lc::store_response(clientCache,accepted,&soloSnapshot,platforms[1]);
    if(!check(queries<=3 && clientCache.snapshot(targetActivity).ownerPlatformId==platforms[0]
        && clientCache.snapshot(temporaryActivity).id==0 && clientCache.owned(soloFireteam,platforms[1])
        && clientCache.snapshot(callerRegular.id).id==callerRegular.id,
        "RUN88 full event backlog reconciles exact absent membership without evicting live sessions"))return false;
    std::puts("PASS: RUN88 native Tower return cache: actual server response, full cache, delayed LEFT page, bounded read-only reconciliation");
    for(const auto handle:handles)if(!mm::release_context(handle))return false;
    {const state::ScopedAccountRuntime scope(target);a::membership::AuthoritativeUpdate update{};a::membership::PendingMutation plan{};
        update.peerConnection=old.membership.peerConnection;
        if(!a::membership::prepare_authoritative(world,update,plan) || !a::membership::commit(plan))return false;}
    realtime_store::shutdown();
    std::puts("PASS: social native handoff: real search, leave/node return, exact temporary retirement, separate fireteams, callbacks, reverse roles and permission/capacity/rekey guards");
    return true;
}
}
