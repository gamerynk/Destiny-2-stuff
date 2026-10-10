#pragma once
#include "late_private_route_self_test.h"
#include "server/bap/encrypted/activity_message/membership/activity_membership_route.h"
#include "middleware/gameplay/descriptor/join_descriptor.h"

namespace restoration::tests {
inline bool social_coordinator_startup_self_test() noexcept {
    using namespace dawn;
    namespace a=state::activity;namespace e=server::bap::encrypted;
    namespace wire=middleware::social::realtime;namespace enc=middleware::encoding;
    const auto check=[](bool ok,const char* label){
        if(!ok)std::printf("FAIL: social coordinator startup: %s\n",label);return ok;};
    constexpr std::array<std::uint64_t,3> platforms{
        0x01100001FF920001ULL,0x01100001FF920002ULL,0x01100001FF920003ULL};
    std::array<account_store::DeviceBinding,3> devices{};
    std::array<a::membership::Identity,3> identities{};
    const auto now=static_cast<std::uint64_t>(std::time(nullptr));
    for(std::size_t i=0;i<devices.size();++i) {
        std::array<std::byte,core::identity::device_identity::kPublicKeySize> key{};
        key.front()=std::byte{0x92};key.back()=std::byte{static_cast<unsigned char>(i+1)};
        if(!check(account_store::load_or_create_device(platforms[i],platforms[i],key,devices[i]),"isolated account"))return false;
        identities[i]={0xAF92000000000001ULL+i,-1,-1,0xBB92000000000001ULL+i,
            devices[i].accountSoid,devices[i].runtime.account->characters[0].soid,0};
    }
    for(std::size_t coordinator=0;coordinator<2;++coordinator) {
        realtime_store::shutdown();
        const auto leader=1-coordinator;
        for(std::size_t i=0;i<devices.size();++i)
            if(!check(realtime_store::begin_game_session(platforms[i],devices[i].accountSoid,1,now)
                && realtime_store::mark_gameplay_ready(platforms[i],devices[i].accountSoid,1,now),"live authenticated peers"))return false;
        wire::Request request{};static wire::Response response{};
        const auto friends=+[](std::uint64_t,std::uint64_t) noexcept{return true;};
        const auto run=[&](std::size_t who){return realtime_store::execute(platforms[who],
            devices[who].accountSoid,now,request,friends,response) && response.result==1;};
        const auto create=[&](std::size_t who){request={};request.operation=wire::Operation::createLobby;
            request.valueA=3;request.valueB=6;return run(who);};
        const auto joinLobby=[&](std::size_t who,std::uint64_t id){request={};
            request.operation=wire::Operation::joinLobby;request.lobbyId=id;return run(who);};
        std::array<std::uint64_t,2> parties{};
        for(std::size_t i=0;i<2;++i) {
            if(!check(create(i),"independent central fireteam"))return false;
            parties[i]=response.lobby.id;
            middleware::gameplay::descriptor::JoinEndpoint endpoint{};
            endpoint.machineId=0xAD920001+i;endpoint.address=0xCB00712A;endpoint.port=27016;
            endpoint.onlineSessionId=parties[i];realtime_store::GameplayBootstrapDescriptor descriptor{};
            if(!middleware::gameplay::descriptor::build(endpoint,descriptor))return false;
            request={};request.operation=wire::Operation::publishNativeFireteamDescriptor;request.lobbyId=parties[i];
            request.payloadSize=descriptor.size();std::copy(descriptor.begin(),descriptor.end(),request.payload.begin());
            if(!check(run(i),"native party descriptor published"))return false;
        }
        if(!check(create(coordinator),"coordinator owns native Tower lobby"))return false;
        const auto nativeTower=response.lobby.id;
        if(!check(joinLobby(leader,nativeTower) && joinLobby(coordinator,parties[leader]),
            "accepted reverse in-world fireteam join"))return false;
        wire::Lobby party{},tower{};
        if(!check(realtime_store::lobby_for_member(platforms[coordinator],party)
            && party.id==parties[leader] && party.ownerPlatformId==platforms[leader] && party.memberCount==2
            && realtime_store::lobby_snapshot(nativeTower,tower) && tower.ownerPlatformId==platforms[coordinator]
            && tower.memberCount==2,"native Tower owner differs from central party leader"))return false;
        std::array<a::membership::Identity,2> peers{identities[coordinator],identities[leader]};
        std::array<std::byte,7719> startup{};std::array<std::byte,137> reply{};
        a::PendingAllocation allocation{};std::size_t written{};bool allocated{};std::uint64_t session{};
        {
            const state::ScopedAccountRuntime scope(devices[coordinator].runtime);
            const auto revision=devices[coordinator].runtime.activity->stateRevision;
            if(!check(late_private_startup_wire(peers,"city_tower_social_d2",20,startup),"two-peer Tower svc6 wire"))return false;
            middleware::bap::activity_host_manager::Request parsed{};
            middleware::bap::activity_host_manager::request::selection::StartupReservations native{};
            if(!check(middleware::bap::activity_host_manager::request::parse_request(startup,parsed)
                && middleware::bap::activity_host_manager::request::selection::parse_startup_reservations(parsed.protobuf,native)
                && native.present && native.members.count==2
                && native.members.peers[0].accountSoid==devices[coordinator].accountSoid
                && native.members.peers[1].field5==identities[leader].opaqueSoid,
                "actual wire parser retains both current account/character identities"))return false;
            if(!check(!e::activity_message::membership::authorize_reservations(std::span(native.members.peers).first(2))
                && !e::activity_message::membership::authorize_reservations({}),
                "type13 and release permissions remain leader-only"))return false;
            if(!check(e::activity_host_manager::encode_response(startup,reply,written,allocation,allocated)
                && allocated && written==137 && allocation.socialWorld
                && allocation.startupOwner.accountSoid==devices[coordinator].accountSoid,
                "nonleader native coordinator gets authenticated social startup"))return false;
            if(!check(allocation.startupReservations[0].accountSoid==devices[leader].accountSoid
                && devices[coordinator].runtime.activity->stateRevision==revision,
                "leader reserved without changing ownership or committing early"))return false;
            const auto accepted=allocation;
            if(!check(!e::activity_host_manager::encode_response(startup,std::span(reply).first(136),
                written,allocation,allocated) && !allocated && written==0
                && devices[coordinator].runtime.activity->stateRevision==revision,"short output cannot publish"))return false;
            const auto reject=[&](std::span<const a::membership::Identity> invalid){
                if(!late_private_startup_wire(invalid,"city_tower_social_d2",20,startup))return false;
                return !e::activity_host_manager::encode_response(startup,reply,written,allocation,allocated)
                    && !allocated && written==0 && devices[coordinator].runtime.activity->stateRevision==revision;};
            auto bad=peers;bad[1]=identities[2];
            if(!check(reject(bad),"unrelated authenticated account denied"))return false;
            bad=peers;bad[1].opaqueSoid=identities[2].opaqueSoid;
            if(!check(reject(bad),"foreign character denied"))return false;
            bad=peers;bad[0]=identities[leader];
            if(!check(reject(std::span(bad).first(1)),"startup cannot omit caller identity"))return false;
            bad=peers;bad[1].memberKey=bad[0].memberKey;
            if(!check(reject(bad),"duplicate native member key denied"))return false;
            bad=peers;bad[0].memberKey=0;
            if(!check(reject(bad) && reject({}),"missing native identity or empty snapshot denied"))return false;
            if(!check(late_private_startup_wire(peers,"edz_freeroam",30,startup)
                && e::activity_host_manager::encode_response(startup,reply,written,allocation,allocated)
                && allocated && !allocation.socialWorld && allocation.startupOwner.memberKey==0,
                "private startup nonleader reservation policy unchanged"))return false;
            e::ServiceOutcome outcome{};outcome.transaction=accepted;e::transactions::Publication publication{};
            if(!check(e::transactions::commit(outcome,publication) && publication.activitySessionId!=0,
                "real social startup transaction commits"))return false;
            session=publication.activitySessionId;
        }
        for(const auto who:{coordinator,leader}) {
            const state::ScopedAccountRuntime scope(devices[who].runtime);const auto& id=identities[who];
            std::array<std::byte,79> body{};auto out=std::span(body);
            enc::write_u64_be(out.first<8>(),session);body[8]=std::byte{1};
            enc::write_u32_be(out.subspan<9,4>(),3);enc::write_u32_be(out.subspan<13,4>(),58);
            enc::bits::Writer bits(out.subspan(21));bool valid=true;
            const auto write=[&](std::uint64_t value,std::uint8_t width){valid=bits.write(value,width)&&valid;};
            write(0x92ABCDEF,32);write(session,64);
            for(unsigned b=0;b<8;++b)write((id.memberKey>>(8*b))&255,8);
            write(0,10);write(0x7FFFFFFF,32);write(id.joinIdentity,64);
            write(id.accountSoid,64);write(id.opaqueSoid,64);write(id.secondaryOpaque,64);write(0,6);
            std::size_t bytes{};e::activity_message::ActivityPlan plan{};bool transaction{};
            if(!check(valid && bits.finish(bytes) && bytes==58
                && e::activity_message::process(0,body,plan,transaction) && transaction,
                "both native type3 joins accepted after nonleader startup"))return false;
            e::ServiceOutcome outcome{};outcome.transaction=plan;e::transactions::Publication publication{};
            a::membership::PendingMutation identity{};
            if(!check(e::transactions::commit(outcome,publication) && a::is_joined(session)
                && a::membership::prepare_identity(session,id,identity) && a::membership::commit(identity),
                "native join/identity commits for both players"))return false;
        }
        if(!check(a::shared::public_peers(devices[0].accountSoid,devices[1].accountSoid)
            && a::shared::public_peers(devices[1].accountSoid,devices[0].accountSoid),"shared world signal authorization"))return false;
        for(std::size_t i=0;i<2;++i)a::shared::disconnect(devices[i].runtime,session);
        if(!check(realtime_store::remove_lobby_member(parties[leader],platforms[coordinator],platforms[coordinator])
            && realtime_store::remove_lobby_member(nativeTower,platforms[coordinator],platforms[coordinator]),"depart coordinator"))return false;
        {
            const state::ScopedAccountRuntime scope(devices[coordinator].runtime);
            if(!check(late_private_startup_wire(peers,"city_tower_social_d2",20,startup)
                && !e::activity_host_manager::encode_response(startup,reply,written,allocation,allocated) && !allocated,
                "departed caller cannot reuse old peer snapshot"))return false;
        }
    }
    realtime_store::shutdown();
    std::puts("PASS: social coordinator startup: real reverse lobby join, svc6/7, both native type3 joins, two role directions and strict identity/permission controls");
    return true;
}
}
