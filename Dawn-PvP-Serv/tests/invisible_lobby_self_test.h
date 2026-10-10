#pragma once
#include "../RestorationServer/src/realtime_store.h"
#include "middleware/gameplay/descriptor/join_descriptor.h"

namespace restoration::tests {
inline bool invisible_lobby_self_test() noexcept {
    namespace wire = dawn::middleware::social::realtime;
    namespace descriptor = dawn::middleware::gameplay::descriptor;
    constexpr std::uint64_t owner=0x01100001000000A1ULL, guest=0x01100001000000A2ULL;
    constexpr std::uint64_t accountA=0x9EAA3000000000A1ULL, accountB=0x9EAA3000000000A2ULL;
    const auto friends=+[](std::uint64_t a,std::uint64_t b) noexcept {
        return (a==owner && b==guest)||(a==guest && b==owner);
    };
    const auto check=[](bool ok,const char* label) noexcept {
        if(!ok)std::printf("FAIL: invisible lobby: %s\n",label);return ok;
    };
    realtime_store::shutdown();
    std::uint64_t now=100;
    wire::Request request{};static wire::Response response{};
    const auto run=[&](std::uint64_t platform) noexcept {
        return realtime_store::execute(platform,platform==owner?accountA:accountB,now,
            request,friends,response) && response.result==1;
    };
    const auto create=[&](std::uint64_t platform,int type) noexcept {
        request={};request.operation=wire::Operation::createLobby;request.valueA=type;request.valueB=6;
        return run(platform);
    };
    const auto publish=[&](std::uint64_t platform,std::uint64_t lobby,
                           realtime_store::GameplayBootstrapDescriptor& bytes) noexcept {
        descriptor::JoinEndpoint endpoint{};endpoint.machineId=platform^0xFF00000000000000ULL;
        endpoint.address=0xCB00712A;endpoint.port=27016;endpoint.onlineSessionId=lobby;
        if(!descriptor::build(endpoint,bytes))return false;
        request={};request.operation=wire::Operation::publishNativeFireteamDescriptor;
        request.lobbyId=lobby;request.payloadSize=bytes.size();
        std::copy(bytes.begin(),bytes.end(),request.payload.begin());return run(platform);
    };
    const auto leave=[&](std::uint64_t platform,std::uint64_t lobby) noexcept {
        request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=lobby;
        return run(platform);
    };
    const auto join=[&](std::uint64_t platform,std::uint64_t lobby) noexcept {
        request={};request.operation=wire::Operation::joinLobby;request.lobbyId=lobby;
        return run(platform);
    };
    for(const auto platform:{owner,guest}) {
        const auto account=platform==owner?accountA:accountB;
        if(!check(realtime_store::begin_game_session(platform,account,1,now)
            && realtime_store::mark_gameplay_ready(platform,account,1,now),"authenticated setup"))return false;
    }
    request.operation=wire::Operation::ensureSoloLobby;request.valueA=1;request.valueB=6;
    if(!check(run(owner),"initial bootstrap"))return false;
    const auto bootstrap=response.lobby.id;
    if(!check(create(owner,3) && response.lobby.id==bootstrap && response.lobby.type==3,
        "native invisible CreateLobby adopts only the unclaimed bootstrap, preserving requested type"))return false;
    const auto fireteamA=response.lobby.id;
    realtime_store::GameplayBootstrapDescriptor bytesA{},bytesB{},copied{};
    if(!check(publish(owner,fireteamA,bytesA),"publish exact owner group-zero route"))return false;
    if(!check(create(owner,3) && response.lobby.id!=fireteamA && response.lobby.type==3,
        "second invisible CreateLobby is a distinct activity session"))return false;
    const auto activityA=response.lobby.id;
    realtime_store::Presence presence{};wire::Lobby lobby{};
    if(!check(realtime_store::presence(owner,now,presence) && presence.joinable
        && presence.lobbyId==fireteamA
        && realtime_store::lobby_for_member(owner,lobby) && lobby.id==fireteamA
        && realtime_store::native_fireteam_descriptor(fireteamA,owner,copied) && copied==bytesA,
        "activity creation preserves native fireteam presence, membership and exact route"))return false;
    if(!check(!create(owner,3) && response.result==25
        && realtime_store::native_fireteam_descriptor(fireteamA,owner,copied) && copied==bytesA,
        "third invisible lobby refused without destroying the existing two"))return false;
    if(!check(create(guest,3),"guest own invisible fireteam"))return false;
    const auto fireteamB=response.lobby.id;
    if(!check(publish(guest,fireteamB,bytesB) && create(guest,3),"guest route and activity"))return false;
    const auto activityB=response.lobby.id;
    if(!check(leave(guest,activityB) && join(guest,activityA),"native public posse join"))return false;
    if(!check(realtime_store::presence(guest,now,presence) && presence.joinable
        && presence.lobbyId==fireteamB
        && realtime_store::lobby_for_member(owner,lobby) && lobby.id==fireteamA && lobby.memberCount==1
        && realtime_store::lobby_for_member(guest,lobby) && lobby.id==fireteamB && lobby.memberCount==1,
        "sharing a public posse does not merge separate fireteams"))return false;
    request={};request.operation=wire::Operation::queryLobby;request.lobbyId=activityA;
    if(!check(run(guest) && response.lobby.id==activityA && response.lobby.memberCount==2
        && response.eventCount==0 && response.cursor==0,"per-ID query returns exact activity members without draining events"))return false;
    request.operation=wire::Operation::poll;request.lobbyId=0;
    if(!check(run(guest) && response.eventCount>0,"query leaves prior callbacks available to the next poll"))return false;
    request={};request.operation=wire::Operation::queryLobby;
    if(!check(run(guest) && response.lobby.id==fireteamB && response.eventCount==0,
        "zero-ID selection query returns fireteam, not most recent activity join"))return false;
    request.lobbyId=fireteamA;
    if(!check(!run(guest) && response.result==15 && response.lobby.id==0,
        "nonmember may not query another fireteam's metadata"))return false;
    if(!check(leave(guest,activityA) && leave(owner,activityA)
        && realtime_store::native_fireteam_descriptor(fireteamA,owner,copied) && copied==bytesA
        && realtime_store::native_fireteam_descriptor(fireteamB,guest,copied) && copied==bytesB,
        "explicit public-activity departure preserves both native fireteams"))return false;
    if(!check(join(guest,fireteamA)
        && realtime_store::lobby_for_member(guest,lobby) && lobby.id==fireteamA && lobby.memberCount==2,
        "real JoinLobby for the published fireteam binds its exact membership"))return false;
    if(!check(!publish(guest,fireteamB,bytesB) && response.result==15
        && realtime_store::presence(guest,now,presence) && presence.lobbyId==fireteamA,
        "late superseded owner publication cannot undo a real fireteam join"))return false;
    if(!check(!leave(guest,fireteamB)
        && realtime_store::presence(guest,now,presence) && presence.lobbyId==fireteamA && presence.joinable,
        "delayed leave of already retired solo lobby cannot remove joined fireteam"))return false;
    if(!check(leave(guest,fireteamA)
        && realtime_store::lobby_for_member(owner,lobby) && lobby.memberCount==1,
        "explicit fireteam leave still removes the actual member"))return false;
    request={};request.operation=wire::Operation::queryLobby;request.lobbyId=fireteamA;
    if(!check(!run(guest) && response.result==15 && response.lobby.id==0,
        "departed member cannot continue reading private lobby metadata"))return false;
    if(!check(create(owner,3),"new activity after prior departure"))return false;
    const auto replacement=response.lobby.id;
    if(!check(realtime_store::begin_game_session(owner,accountA,2,++now)
        && !realtime_store::lobby_snapshot(fireteamA,lobby)
        && !realtime_store::lobby_snapshot(replacement,lobby)
        && !realtime_store::native_fireteam_descriptor(fireteamA,owner,copied),
        "new authenticated generation retires all old lobby memberships and routes"))return false;
    realtime_store::shutdown();
    std::puts("PASS: invisible lobby: distinct sessions, exact fireteam route, public separation, per-ID authorization, quota and cleanup");
    return true;
}
}
