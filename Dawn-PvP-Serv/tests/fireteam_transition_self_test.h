#pragma once
#include "../restorationserver/src/realtime_store.h"
#include "middleware/gameplay/descriptor/join_descriptor.h"

namespace restoration::tests {
inline bool fireteam_transition_self_test() noexcept {
    namespace wire = dawn::middleware::social::realtime;
    namespace desc = dawn::middleware::gameplay::descriptor;
    constexpr std::array<std::uint64_t,4> ids{0x0110000100FC0001ULL,
        0x0110000100FC0002ULL,0x0110000100FC0003ULL,0x0110000100FC0004ULL};
    const auto check=[](bool ok,const char* label) {
        if(!ok)std::printf("FAIL: fireteam transition: %s\n",label);return ok;
    };
    const auto friends=+[](std::uint64_t,std::uint64_t) noexcept{return true;};
    const auto noFriends=+[](std::uint64_t,std::uint64_t) noexcept{return false;};
    realtime_store::shutdown();
    struct Cleanup { ~Cleanup(){realtime_store::shutdown();} } cleanup;
    for(const auto id:ids)
        if(!realtime_store::begin_game_session(id,id,1,100)
            || !realtime_store::mark_gameplay_ready(id,id,1,100))return false;
    wire::Request req{};static wire::Response reply{};
    std::array<std::uint64_t,4> cursors{};
    const auto run=[&](std::size_t i,bool allow=true){req.cursor=cursors[i];
        const bool ok=realtime_store::execute(ids[i],ids[i],100,req,allow?friends:noFriends,reply);
        cursors[i]=reply.cursor;return ok && reply.result==1;};
    const auto create=[&](std::size_t i){req={};req.operation=wire::Operation::createLobby;
        req.valueA=3;req.valueB=6;return run(i);};
    const auto join=[&](std::size_t i,std::uint64_t lobby,bool allow=true){req={};
        req.operation=wire::Operation::joinLobby;req.lobbyId=lobby;return run(i,allow);};
    const auto leave=[&](std::size_t i,std::uint64_t lobby){req={};
        req.operation=wire::Operation::leaveLobby;req.lobbyId=lobby;return run(i);};
    const auto publish=[&](std::size_t i,std::uint64_t lobby,bool valid=true){
        desc::JoinEndpoint ep{};ep.machineId=ids[i];ep.address=0xCB00712A;ep.port=27016;
        ep.onlineSessionId=valid?lobby:lobby+100;realtime_store::GameplayBootstrapDescriptor bytes{};
        if(!desc::build(ep,bytes))return false;
        req={};req.operation=wire::Operation::publishNativeFireteamDescriptor;req.lobbyId=lobby;
        req.payloadSize=bytes.size();std::copy(bytes.begin(),bytes.end(),req.payload.begin());return run(i);};
    const auto contains=[](const wire::Lobby& lobby,std::uint64_t id){
        return std::any_of(lobby.members.begin(),lobby.members.begin()+lobby.memberCount,
            [id](const auto& member){return member.platformId==id;});};
    wire::Lobby snap{};
    if(!create(0))return false;
    const auto old=reply.lobby.id;
    if(!publish(0,old) || !join(1,old) || !join(2,old) || !create(1))return false;
    const auto activity=reply.lobby.id;
    if(!join(0,activity) || !leave(0,activity) || !create(0))return false;
    const auto replacement=reply.lobby.id;
    if(!check(!publish(0,replacement,false) && reply.result==8
        && realtime_store::lobby_snapshot(old,snap) && snap.memberCount==3,
        "invalid descriptor cannot depart old party"))return false;
    if(!check(publish(0,replacement) && realtime_store::lobby_snapshot(old,snap)
        && snap.memberCount==2 && !contains(snap,ids[0]) && snap.ownerPlatformId==ids[1],
        "replacement g0 removes only owner old membership; survivors retain lobby"))return false;
    if(!check(!publish(0,old) && reply.result==15 && !leave(0,old)
        && realtime_store::lobby_for_member(ids[0],snap) && snap.id==replacement,
        "late publish/leave cannot restore or erase replaced membership"))return false;
    if(!check(create(0),"leader can create match activity after replacing g0"))return false;
    const auto match=reply.lobby.id;
    if(!check(!create(0) && reply.result==25,"two-session bound is unchanged"))return false;
    if(!check(!join(1,replacement,false) && reply.result==15
        && realtime_store::lobby_snapshot(old,snap) && snap.memberCount==2,
        "unauthorized handoff preserves both memberships"))return false;
    req={};req.operation=wire::Operation::setMemberLimit;req.lobbyId=replacement;req.valueA=1;
    if(!run(0))return false;
    if(!check(!join(1,replacement) && reply.result==25
        && realtime_store::lobby_snapshot(old,snap) && snap.memberCount==2,
        "full target preserves old party"))return false;
    req.valueA=6;req.operation=wire::Operation::setMemberLimit;req.lobbyId=replacement;
    if(!run(0))return false;
    if(!check(join(1,replacement) && realtime_store::lobby_snapshot(old,snap)
        && snap.memberCount==1 && snap.ownerPlatformId==ids[2]
        && realtime_store::lobby_snapshot(activity,snap) && contains(snap,ids[1]),
        "authorized join moves only caller, even during old owner migration"))return false;
    if(!check(join(2,replacement) && join(3,replacement)
        && !realtime_store::lobby_snapshot(old,snap),"all four share one current fireteam"))return false;
    for(std::size_t i=0;i<ids.size();++i)
        if(!check(realtime_store::lobby_for_member(ids[i],snap) && snap.id==replacement
            && snap.memberCount==4 && realtime_store::shares_fireteam(ids[0],ids[i==0?1:i]),
            "reservation membership agrees for every participant"))return false;
    if(!check(join(1,match) && join(2,match) && join(3,match)
        && !realtime_store::lobby_snapshot(activity,snap)
        && realtime_store::lobby_snapshot(match,snap) && snap.memberCount==4,
        "all participants admit to match while retaining their fireteam"))return false;
    std::puts("PASS: Cloudberry fireteam replacement: four players, migrated owner, quota, authorization, capacity, stale callbacks, activity admission");
    return true;
}
}
