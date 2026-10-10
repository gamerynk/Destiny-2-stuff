#pragma once
#include "shared_activity_join_self_test.h"
#include "server/bap/encrypted/push/activity/internal.h"
#include "middleware/bap/activity_message/activity_player_assignment.h"
#include "state/build_data/scenarios/scenario_catalog.h"

namespace restoration::tests {
inline bool activity_player_team_self_test(std::uint64_t fixtureSession) noexcept {
    using namespace dawn;
    namespace activity=state::activity;
    namespace member=activity::membership;
    namespace shared=activity::shared;
    namespace push=server::bap::encrypted::push::activity;
    const auto check=[](bool ok,const char* why) noexcept {
        if(!ok)std::printf("FAIL: private teams: %s\n",why);return ok;
    };
    activity::destination::DestinationSelection destination{};
    if(fixtureSession!=0) {
        if(!activity::destination::snapshot(fixtureSession,destination))return false;
    } else {
        destination=core::settings::get().initialActivityDefaults.defaultDestination.selection;
    }
    destination.packageName={};constexpr char map[]="pvp_ness";
    std::copy_n(map,sizeof(map)-1,destination.packageName.begin());
    destination.packageNameLength=sizeof(map)-1;destination.activityIndex=470;
    namespace catalog=state::build_data::scenarios;
    static std::array<catalog::Definition,catalog::kDefinitionCapacity> savedDefinitions{};
    static std::array<catalog::RosterGroup,catalog::kRosterGroupCapacity> savedGroups{};
    std::size_t definitionsCount{},groupsCount{};
    if(!catalog::snapshot(savedDefinitions,definitionsCount)||!catalog::snapshot_groups(savedGroups,groupsCount))return false;
    struct RestoreCatalog{std::size_t d,g;~RestoreCatalog(){static_cast<void>(catalog::replace(std::span(savedDefinitions).first(d),std::span(savedGroups).first(g)));}} restoreCatalog{definitionsCount,groupsCount};
    catalog::Definition layout{};std::copy_n(map,sizeof(map)-1,layout.name.begin());
    layout.nameLength=sizeof(map)-1;layout.bubbleCount=1;layout.bubbleStates[0]=0x80;
    layout.bubbleStateCounts[0]=1;layout.bubbleHashes[0]=1;layout.rosterGroupCount=1;
    catalog::RosterGroup group{};group.registryKey=0x39774EE1;group.slotCount=1;
    group.slotTypes[0]=13;group.slotFlags[0]=3;
    if(!check(catalog::replace(std::span(&layout,1),std::span(&group,1)),"bounded isolated layout fixture"))return false;
    static std::array<state::AccountState,2> accounts{};
    static std::array<activity::ActivityState,2> worlds{};
    static std::array<std::byte,sizeof(state::State)> before{};
    std::memcpy(before.data(),&state::runtime::storage::g_state,before.size());
    for(unsigned i=0;i<2;++i){accounts[i]=state::account_snapshot();accounts[i].primarySoid=0x8000300100100100ULL+(std::uint64_t{i}<<32);worlds[i]={};}
    const std::array<state::AccountRuntimeContext,2> contexts{{{&accounts[0],&worlds[0]},{&accounts[1],&worlds[1]}}};
    const std::array<member::Identity,2> ids{{
        {101,-1,-1,201,accounts[0].primarySoid,accounts[0].primarySoid+2,501},
        {102,-1,-1,202,accounts[1].primarySoid,accounts[1].primarySoid+2,502}}};
    struct Cleanup{~Cleanup(){server::gameplay::group::reset_host_sessions();}} cleanup;
    std::uint64_t sessionId{};
    activity::entity_slots::PendingMutation join{};
    state::AccountRuntimeContext source{};
    {
        const state::ScopedAccountRuntime scope(contexts[0]);activity::PendingAllocation allocation{};
        if(!check(activity::prepare_session(destination,sessionId,allocation)
            &&activity::prepare_startup_reservations(allocation,ids)&&activity::commit(allocation)
            &&activity::entity_slots::prepare_join(sessionId,ids[0].memberKey,64,8,join)
            &&activity::entity_slots::commit(join),"allocate host"))return false;
    }
    if(!shared::owner_context(sessionId,source))return false;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        if(!check(shared::prepare_join(source,sessionId,ids[1].memberKey,ids[1].opaqueSoid,64,8,join)
            &&activity::entity_slots::commit(join),"admit second authenticated account"))return false;
    }
    for(unsigned i=0;i<2;++i){
        const state::ScopedAccountRuntime scope(contexts[i]);member::PendingMutation p{};
        member::AuthoritativeUpdate update{};update.peerConnection.playerTeam={static_cast<std::int8_t>(1-i),static_cast<std::int8_t>(1-i),true,true};
        if(!check(member::prepare_identity(sessionId,ids[i],p)&&member::commit(p)
            &&member::prepare_authoritative(sessionId,update,p)&&member::commit(p),"commit owned team"))return false;
    }
    for(unsigned i=0;i<2;++i){
        const state::ScopedAccountRuntime scope(contexts[i]);member::PendingMutation p{};shared::Projection view{};
        if(!check(member::prepare_refresh(sessionId,0,-1,p)&&shared::project(sessionId,p.snapshot,view)
            &&view.localSlot==i&&view.joinedMemberMask==3
            &&view.peerConnections[0].playerTeam.assignment==1
            &&view.peerConnections[1].playerTeam.assignment==0
            &&member::player_team(sessionId).assignment==1-static_cast<int>(i),"canonical projection and own query do not alias"))return false;
        if(!check(shared_joined_notification_test(sessionId,p,3),"real encrypted membership staging"))return false;
        static server::bap::Scratch scratch{};server::bap::Session session{};
        session.activitySessionId=sessionId;session.activityCharacterSoid=ids[i].opaqueSoid;
        push::message::Snapshot roster{};std::array<char,40> name{};std::size_t nameSize{};
        if(!check(push::build_roster_snapshot(session,scratch,roster,name,nameSize,false)==push::RosterOutcome::published
            &&roster.playerAssignment==1-i&&roster.playerTeamIndex==1-i,"actual roster builder uses own committed team"))return false;
        member::AuthoritativeUpdate update{};update.peerConnection.playerTeam={static_cast<std::int8_t>(i),static_cast<std::int8_t>(i),true,true};
        if(!check(member::prepare_authoritative(sessionId,update,p)&&p.hasSnapshot
            &&push::build_roster_snapshot(session,scratch,roster,name,nameSize,false,&p.snapshot.peerConnection.playerTeam)==push::RosterOutcome::published
            &&roster.playerAssignment==i&&member::player_team(sessionId).assignment==1-static_cast<int>(i),"prepared roster agrees with pending type12 without early state mutation"))return false;
    }
    shared::disconnect(contexts[1],sessionId);
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        if(!check(member::player_team(sessionId)==core::protocol::ActivityPlayerTeam{},"departed player query cannot reuse assignment"))return false;
    }
    shared::disconnect(contexts[0],sessionId);
    if(!check(std::memcmp(before.data(),&state::runtime::storage::g_state,before.size())==0,"ambient account state unchanged"))return false;
    std::puts("PASS: private teams: authenticated two-account join/retention, canonical projection, encrypted membership, own and prepared roster assignments, departure isolation");
    return true;
}
}
