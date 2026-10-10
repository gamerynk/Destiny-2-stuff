#pragma once
#include "../RestorationServer/src/realtime_store.h"
#include "middleware/gameplay/descriptor/join_descriptor.h"

namespace restoration::tests {
inline bool invisible_lobby_handoff_self_test() noexcept {
    namespace wire = dawn::middleware::social::realtime;
    namespace descriptor = dawn::middleware::gameplay::descriptor;
    constexpr std::uint64_t host=0x01100001000000B1ULL, guest=0x01100001000000B2ULL;
    constexpr std::uint64_t accountA=0x9EAA3000000000B1ULL, accountB=0x9EAA3000000000B2ULL;
    constexpr std::uint64_t companion=0x01100001000000B3ULL, accountC=0x9EAA3000000000B3ULL;
    const auto friends=+[](std::uint64_t a,std::uint64_t b) noexcept {
        return (a==host && b==guest)||(a==guest && b==host)
            || (a==companion && b==guest)||(a==guest && b==companion);
    };
    const auto check=[](bool ok,const char* label) noexcept {
        if(!ok)std::printf("FAIL: invisible handoff: %s\n",label);return ok;
    };
    realtime_store::shutdown();
    wire::Request request{}; static wire::Response response{};
    std::uint64_t hostCursor=0, guestCursor=0, companionCursor=0;
    const auto run=[&](std::uint64_t platform) noexcept {
        auto& cursor=platform==host?hostCursor:platform==guest?guestCursor:companionCursor;
        request.cursor=cursor;
        const auto ok=realtime_store::execute(platform,
            platform==host?accountA:platform==guest?accountB:accountC,100,request,friends,response);
        cursor=response.cursor;
        return ok && response.result==1;
    };
    const auto create=[&](std::uint64_t platform) noexcept {
        request={};request.operation=wire::Operation::createLobby;request.valueA=3;request.valueB=6;
        return run(platform);
    };
    const auto publish=[&](std::uint64_t platform,std::uint64_t lobby) noexcept {
        descriptor::JoinEndpoint endpoint{};endpoint.machineId=platform^0xAB00000000000000ULL;
        endpoint.address=0xCB00712A;endpoint.port=27016;endpoint.onlineSessionId=lobby;
        realtime_store::GameplayBootstrapDescriptor bytes{};
        if(!descriptor::build(endpoint,bytes))return false;
        request={};request.operation=wire::Operation::publishNativeFireteamDescriptor;
        request.lobbyId=lobby;request.payloadSize=bytes.size();
        std::copy(bytes.begin(),bytes.end(),request.payload.begin());return run(platform);
    };
    const auto join=[&](std::uint64_t lobby) noexcept {
        request={};request.operation=wire::Operation::joinLobby;request.lobbyId=lobby;return run(guest);
    };
    const auto exactDeparture=[&](std::uint64_t retired,std::uint64_t entered) noexcept {
        bool left=false, joined=false;
        for(std::size_t i=0;i<response.eventCount;++i) {
            const auto& event=response.events[i];
            if(event.kind!=wire::EventKind::memberChanged || event.memberPlatformId!=guest
                || event.payloadSize!=sizeof(std::uint32_t))continue;
            std::uint32_t flags=0;std::memcpy(&flags,event.payload.data(),sizeof(flags));
            if(event.lobbyId==retired && flags==2)left=true;
            if(event.lobbyId==entered && flags==1)joined=true;
        }
        return left && joined;
    };
    for(const auto platform:{host,guest}) {
        const auto account=platform==host?accountA:accountB;
        if(!check(realtime_store::begin_game_session(platform,account,1,100)
            && realtime_store::mark_gameplay_ready(platform,account,1,100),"setup"))return false;
    }
    if(!check(create(host),"host fireteam"))return false;
    const auto hostFireteam=response.lobby.id;
    if(!check(publish(host,hostFireteam) && create(host),"host activity"))return false;
    const auto hostActivity=response.lobby.id;
    if(!check(create(guest),"guest fireteam"))return false;
    const auto oldFireteam=response.lobby.id;
    if(!check(publish(guest,oldFireteam) && create(guest),"guest activity"))return false;
    const auto oldActivity=response.lobby.id;
    wire::Lobby lobby{};
    request={};request.operation=wire::Operation::setMemberLimit;request.lobbyId=hostFireteam;
    request.valueA=1;
    if(!check(run(host) && !join(hostFireteam) && response.result==25
        && realtime_store::lobby_snapshot(oldFireteam,lobby)
        && realtime_store::lobby_snapshot(oldActivity,lobby),"full target cannot retire source"))return false;
    request={};request.operation=wire::Operation::setMemberLimit;request.lobbyId=hostFireteam;
    request.valueA=6;if(!check(run(host),"restore target capacity"))return false;
    if(!check(realtime_store::begin_game_session(companion,accountC,1,100)
        && realtime_store::mark_gameplay_ready(companion,accountC,1,100),"companion setup"))return false;
    request={};request.operation=wire::Operation::joinLobby;request.lobbyId=oldFireteam;
    if(!check(run(companion) && !join(hostActivity) && response.result==25
        && realtime_store::lobby_snapshot(oldFireteam,lobby) && lobby.memberCount==2,
        "public activity join cannot depart or split a shared source fireteam"))return false;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=oldFireteam;
    if(!check(run(companion),"companion leaves voluntarily"))return false;
    if(!check(!join(hostActivity) && response.result==25
        && realtime_store::lobby_snapshot(oldFireteam,lobby)
        && realtime_store::lobby_snapshot(oldActivity,lobby),
        "unrelated public posse cannot retire caller's fireteam at quota"))return false;
    request={};request.operation=wire::Operation::setJoinable;request.lobbyId=hostFireteam;
    if(!check(run(host) && !join(hostFireteam) && response.result==15
        && realtime_store::lobby_snapshot(oldFireteam,lobby),"closed target preserves old sessions"))return false;
    request={};request.operation=wire::Operation::setJoinable;request.lobbyId=hostFireteam;request.valueA=1;
    if(!check(run(host),"reopen target"))return false;
    request={};request.operation=wire::Operation::joinLobby;request.lobbyId=hostFireteam;
    const auto noFriends=+[](std::uint64_t,std::uint64_t) noexcept {return false;};
    if(!check(realtime_store::execute(guest,accountB,100,request,noFriends,response)
        && response.result==15 && realtime_store::lobby_snapshot(oldFireteam,lobby),
        "unauthorized join cannot retire anything"))return false;
    if(!check(join(hostFireteam) && response.lobby.id==hostFireteam
        && response.lobby.memberCount==2,
        "native target fireteam joins before delayed old-session leaves"))return false;
    if(!check(exactDeparture(oldFireteam,hostFireteam),"exact old-fireteam left and target entered callbacks"))return false;
    if(!check(!realtime_store::lobby_snapshot(oldFireteam,lobby)
        && realtime_store::lobby_snapshot(oldActivity,lobby) && lobby.memberCount==1
        && realtime_store::lobby_for_member(guest,lobby) && lobby.id==hostFireteam,
        "only exact superseded solo fireteam retired; old activity and new binding preserved"))return false;
    if(!check(join(hostActivity) && response.lobby.id==hostActivity && response.lobby.memberCount==2
        && !realtime_store::lobby_snapshot(oldActivity,lobby)
        && realtime_store::lobby_for_member(guest,lobby) && lobby.id==hostFireteam,
        "target posse follows accepted fireteam; only old solo activity retired"))return false;
    if(!check(exactDeparture(oldActivity,hostActivity),"exact old-activity left and target entered callbacks"))return false;
    for(const auto retired:{oldFireteam,oldActivity}) {
        request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=retired;
        (void)run(guest);
        if(!check(realtime_store::lobby_for_member(guest,lobby) && lobby.id==hostFireteam
            && lobby.memberCount==2 && realtime_store::lobby_snapshot(hostActivity,lobby)
            && lobby.memberCount==2,"delayed native old-handle leave cannot erase target sessions"))return false;
    }
    if(!check(!create(host) && response.result==25,"owner's two invisible limit still enforced"))return false;
    // RUN85: retail Leave Fireteam creates its replacement g0 before releasing the foreign g0
    // and posse; rejecting that create strands it without an online-session ID
    request={};request.operation=wire::Operation::createLobby;request.valueA=3;request.valueB=0;
    if(!check(!run(guest) && response.result==8
        && realtime_store::lobby_snapshot(hostFireteam,lobby) && lobby.memberCount==2
        && realtime_store::lobby_snapshot(hostActivity,lobby) && lobby.memberCount==2,
        "invalid replacement preserves both old memberships"))return false;
    request={};request.operation=wire::Operation::ensureSoloLobby;request.valueA=3;request.valueB=6;
    if(!check(run(guest) && realtime_store::lobby_snapshot(hostFireteam,lobby) && lobby.memberCount==2,
        "background bootstrap cannot depart a shared fireteam"))return false;
    if(!check(create(companion),"unrelated activity owner setup"))return false;
    const auto unrelatedActivity=response.lobby.id;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=hostActivity;
    if(!check(run(guest) && join(unrelatedActivity) && !create(guest) && response.result==25
        && realtime_store::lobby_snapshot(hostFireteam,lobby) && lobby.memberCount==2
        && realtime_store::lobby_snapshot(unrelatedActivity,lobby) && lobby.memberCount==2,
        "mixed-owner sessions cannot be guessed as a solo handoff"))return false;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=unrelatedActivity;
    if(!check(run(guest) && join(hostActivity),"restore exact paired foreign sessions"))return false;
    std::array<std::uint64_t,wire::kLobbyCapacity> fillerLobbies{};
    std::size_t fillerCount=0;
    for(std::size_t i=0;i<fillerLobbies.size();++i) {
        const auto platform=0x0110000100001000ULL+i, account=0x9EAA300000001000ULL+i;
        if(!check(realtime_store::begin_game_session(platform,account,1,100)
            && realtime_store::mark_gameplay_ready(platform,account,1,100),"capacity fixture identity"))return false;
        request={};request.operation=wire::Operation::createLobby;request.valueA=3;request.valueB=6;
        if(!check(realtime_store::execute(platform,account,100,request,friends,response),"capacity fixture create"))return false;
        if(response.result==25)break;
        if(!check(response.result==1,"capacity fixture result"))return false;
        fillerLobbies[fillerCount++]=response.lobby.id;
    }
    if(!check(!create(guest) && response.result==25 && fillerCount>0
        && realtime_store::lobby_snapshot(hostFireteam,lobby) && lobby.memberCount==2
        && realtime_store::lobby_snapshot(hostActivity,lobby) && lobby.memberCount==2,
        "full lobby table cannot partially depart the caller"))return false;
    for(std::size_t i=0;i<fillerCount;++i) {
        request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=fillerLobbies[i];
        if(!check(realtime_store::execute(0x0110000100001000ULL+i,0x9EAA300000001000ULL+i,
            100,request,friends,response) && response.result==1,"release capacity fixture"))return false;
    }
    if(!check(create(guest) && response.lobby.id!=hostFireteam && response.lobby.id!=hostActivity
        && response.lobby.ownerPlatformId==guest && response.lobby.memberCount==1,
        "native solo replacement creates before delayed foreign-session leaves"))return false;
    const auto newFireteam=response.lobby.id;
    if(!check(exactDeparture(hostFireteam,newFireteam),
        "solo replacement delivers exact old LEFT and new ENTERED callbacks"))return false;
    if(!check(realtime_store::lobby_snapshot(hostFireteam,lobby) && lobby.memberCount==1
        && lobby.ownerPlatformId==host && realtime_store::lobby_snapshot(hostActivity,lobby)
        && lobby.memberCount==2 && lobby.ownerPlatformId==host,
        "depart only caller's old fireteam; keep host and old activity intact"))return false;
    if(!check(!create(guest) && response.result==25,
        "replacement does not grant a third invisible membership"))return false;
    realtime_store::Presence hostPresence{},guestPresence{};
    if(!check(realtime_store::presence(guest,100,guestPresence) && !guestPresence.joinable,
        "new solo is not advertised without its native descriptor"))return false;
    if(!check(publish(guest,newFireteam) && realtime_store::presence(guest,100,guestPresence)
        && guestPresence.joinable && guestPresence.lobbyId==newFireteam
        && realtime_store::presence(host,100,hostPresence) && hostPresence.joinable
        && hostPresence.lobbyId==hostFireteam,
        "both independent native fireteams are joinable after replacement publication"))return false;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=hostFireteam;
    if(!check(!run(guest) && realtime_store::lobby_for_member(guest,lobby) && lobby.id==newFireteam,
        "delayed old fireteam leave cannot remove replacement"))return false;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=hostActivity;
    if(!check(run(guest) && realtime_store::lobby_snapshot(hostActivity,lobby) && lobby.memberCount==1
        && lobby.ownerPlatformId==host && create(guest),"old activity leave preserves host; new node launches"))return false;
    const auto newActivity=response.lobby.id;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=newActivity;
    if(!check(run(guest) && create(guest)
        && realtime_store::presence(guest,100,guestPresence) && guestPresence.joinable
        && guestPresence.lobbyId==newFireteam,
        "world-to-world activity replacement preserves solo join route"))return false;
    request={};request.operation=wire::Operation::joinLobby;request.lobbyId=newFireteam;
    if(!check(run(host) && response.lobby.id==newFireteam && response.lobby.memberCount==2,
        "reverse direction can join the recovered solo fireteam"))return false;
    realtime_store::shutdown();
    hostCursor=guestCursor=companionCursor=0;
    if(!check(realtime_store::begin_game_session(host,accountA,2,100)
        && realtime_store::mark_gameplay_ready(host,accountA,2,100)
        && realtime_store::begin_game_session(guest,accountB,2,100)
        && realtime_store::mark_gameplay_ready(guest,accountB,2,100),
        "reverse-owner lifecycle fixture"))return false;
    if(!create(host))return false;
    const auto visitorParty=response.lobby.id;
    if(!publish(host,visitorParty) || !create(guest))return false;
    const auto originalOwnerParty=response.lobby.id;
    if(!publish(guest,originalOwnerParty) || !create(guest))return false;
    const auto ownedTower=response.lobby.id;
    request={};request.operation=wire::Operation::joinLobby;request.lobbyId=ownedTower;
    if(!check(run(host) && join(visitorParty),
        "original Tower owner joins visitor fireteam in place"))return false;
    if(!check(realtime_store::lobby_snapshot(ownedTower,lobby) && lobby.ownerPlatformId==guest
        && lobby.memberCount==2 && realtime_store::lobby_snapshot(visitorParty,lobby)
        && lobby.ownerPlatformId==host && lobby.memberCount==2,
        "Tower ownership is independent of fireteam leadership"))return false;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=ownedTower;
    if(!check(run(host) && !create(guest) && response.result==25
        && realtime_store::lobby_snapshot(visitorParty,lobby) && lobby.memberCount==2,
        "RUN90 caller-owned activity without foreign party leader is not a paired replacement"))return false;
    request={};request.operation=wire::Operation::joinLobby;request.lobbyId=ownedTower;
    if(!check(run(host),"RUN90 restore shared activity membership"))return false;
    if(!check(create(guest),
        "RUN90 original Tower owner can create solo fireteam before leaving visitor party"))return false;
    const auto recoveredParty=response.lobby.id;
    if(!check(recoveredParty!=visitorParty && recoveredParty!=ownedTower
        && exactDeparture(visitorParty,recoveredParty)
        && realtime_store::lobby_snapshot(ownedTower,lobby) && lobby.ownerPlatformId==guest
        && lobby.memberCount==2 && realtime_store::lobby_snapshot(visitorParty,lobby)
        && lobby.ownerPlatformId==host && lobby.memberCount==1,
        "RUN90 retires only caller foreign fireteam, preserving owned shared Tower"))return false;
    if(!check(publish(guest,recoveredParty) && !create(guest) && response.result==25,
        "RUN90 new native fireteam identity and original two-session bound"))return false;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=visitorParty;
    if(!check(!run(guest) && realtime_store::lobby_for_member(guest,lobby) && lobby.id==recoveredParty,
        "RUN90 delayed old fireteam leave cannot erase solo replacement"))return false;
    request={};request.operation=wire::Operation::leaveLobby;request.lobbyId=ownedTower;
    if(!check(run(guest) && realtime_store::lobby_snapshot(ownedTower,lobby)
        && lobby.ownerPlatformId==host && lobby.memberCount==1 && create(guest)
        && response.lobby.id!=recoveredParty && response.lobby.id!=ownedTower,
        "RUN90 survivor retains Tower, returning owner gets a distinct temporary activity"))return false;
    realtime_store::shutdown();
    std::puts("PASS: invisible handoff: native join/create-before-leave, exact membership retirement, reverse join, authorization and two-session bound");
    std::puts("PASS: RUN90 original-Tower-owner leave after in-place visitor-fireteam join; distinct solo/activity identities");
    return true;
}
}
