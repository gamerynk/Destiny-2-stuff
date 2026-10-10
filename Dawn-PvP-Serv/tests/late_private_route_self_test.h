#pragma once
#include "late_private_join_self_test.h"
#include "shared_activity_join_self_test.h"
#include "../RestorationServer/src/account_store.h"
#include "../RestorationServer/src/realtime_store.h"
#include "core/identity/device_identity.h"
#include "core/settings/settings.h"
#include "middleware/encoding/bit_writer.h"
#include "server/bap/encrypted/activity_message/activity_message_route.h"
#include "server/bap/encrypted/activity_host_manager/activity_host_manager_route.h"
#include "middleware/bap/activity_host_manager/request/activity_manager_request.h"
#include "middleware/bap/activity_host_manager/request/selection/activity_manager_reservations.h"
#include "middleware/protobuf/codec.h"
#include "server/bap/encrypted/internal.h"
#include "server/bap/encrypted/transactions/service_outcome_commit.h"
#include <ctime>

namespace restoration::tests {
inline bool late_private_startup_wire(std::span<const dawn::state::activity::membership::Identity> peers,
    std::string_view package,std::uint16_t activity,std::span<std::byte,7719> request) noexcept {
    using namespace dawn;
    std::array<std::byte,6000> snapshot{};middleware::encoding::bits::Writer bits(snapshot);
    bool valid=true;
    const auto write=[&](std::uint64_t value,std::uint8_t width){valid=bits.write(value,width)&&valid;};
    const auto zeros=[&](std::size_t count){while(count>64){write(0,64);count-=64;}write(0,static_cast<std::uint8_t>(count));};
    const auto keyBytes=[&](std::uint64_t value){for(unsigned b=0;b<8;++b)write((value>>(8*b))&255,8);};
    write(0,10);zeros(2048+2048+8192+256+4+1+8+4+32+2);
    write(1,4);write(activity+1,12);write(activity+1,12);zeros(3+5+8+1);
    write(1,1);write(0xE86E3E4C,32);write(1,1);write(0x2EA8FB98,32);write(1,1);
    for(std::size_t c=0;c<40;++c)write(128+(c<package.size()?package[c]:0),8);
    zeros(4);zeros((256+1024)*8);if(peers.size()>32)return false;write(peers.size(),6);
    for(const auto& owner:peers) {
        keyBytes(owner.memberKey);write(0,10);write(0x7FFFFFFF,32);keyBytes(owner.joinIdentity);
        write(owner.accountSoid,64);write(owner.opaqueSoid,64);write(owner.secondaryOpaque,64);
    }
    zeros(64+64+32+10);
    std::size_t size{};if(!valid || !bits.finish(size))return false;
    std::fill(request.begin(),request.end(),std::byte{});request[0]=std::byte{3};
    middleware::protobuf::Writer protobuf(request.subspan(5));
    if(!protobuf.write_length_delimited(2,std::span(snapshot).first(size)))return false;
    for(unsigned b=0;b<4;++b)request[1+b]=std::byte{static_cast<unsigned char>((protobuf.size()>>(b*8))&255)};
    return true;
}
inline bool late_private_startup_wire(const dawn::state::activity::membership::Identity& owner,
    std::string_view package,std::uint16_t activity,std::span<std::byte,7719> request) noexcept {
    return late_private_startup_wire(std::span(&owner,1),package,activity,request);
}
inline bool late_private_route_self_test(std::uint64_t fixtureSession) noexcept {
    using namespace dawn;
    namespace a=state::activity;namespace e=server::bap::encrypted;
    namespace wire=middleware::social::realtime;namespace enc=middleware::encoding;
    const auto check=[](bool ok,const char* label){if(!ok)std::printf("FAIL: late private route: %s\n",label);return ok;};
    a::destination::DestinationSelection destination{};
    if(!a::destination::snapshot(fixtureSession,destination))return false;
    std::array<account_store::DeviceBinding,2> devices{};
    std::array<a::membership::Identity,2> identities{};
    constexpr std::array<std::uint64_t,2> platforms{0x01100001FF820001ULL,0x01100001FF820002ULL};
    const auto now=static_cast<std::uint64_t>(std::time(nullptr));
    for(std::size_t i=0;i<2;++i) {
        std::array<std::byte,core::identity::device_identity::kPublicKeySize> key{};
        key.front()=std::byte{0x82};key.back()=std::byte{static_cast<unsigned char>(i+1)};
        if(!check(account_store::load_or_create_device(platforms[i],platforms[i],key,devices[i]),"isolated device account"))return false;
        const auto& d=devices[i];
        identities[i]={0xAF82000000000001ULL+i,-1,-1,0xBB82000000000001ULL+i,
            d.accountSoid,d.runtime.account->characters[0].soid,0};
        if(!check(realtime_store::begin_game_session(platforms[i],d.accountSoid,1,now)
            && realtime_store::mark_gameplay_ready(platforms[i],d.accountSoid,1,now),"authenticated game session"))return false;
    }
    std::uint64_t session{};
    wire::Request request{};wire::Response response{};
    request.operation=wire::Operation::createLobby;request.valueA=2;request.valueB=6;
    if(!check(realtime_store::execute(platforms[0],devices[0].accountSoid,now,request,nullptr,response)
        && response.result==1,"host central fireteam"))return false;
    const auto lobby=response.lobby.id;
    const auto message=[&](a::membership::Identity id) {
        std::array<std::byte,79> body{};auto out=std::span(body);
        enc::write_u64_be(out.first<8>(),session);body[8]=std::byte{1};
        enc::write_u32_be(out.subspan<9,4>(),3);enc::write_u32_be(out.subspan<13,4>(),58);
        enc::bits::Writer bits(out.subspan(21));bool ok=true;
        const auto write=[&](std::uint64_t value,std::uint8_t width){ok=bits.write(value,width)&&ok;};
        write(0x82ABCDEF,32);write(session,64);
        for(unsigned b=0;b<8;++b)write((id.memberKey>>(8*b))&255,8);
        write(static_cast<std::uint32_t>(id.smallOpaque)+1,10);write(0x7FFFFFFF,32);
        write(id.joinIdentity,64);write(id.accountSoid,64);write(id.opaqueSoid,64);write(id.secondaryOpaque,64);
        write(0,6);std::size_t size{};
        if(!ok || !bits.finish(size) || size!=58)body={};return body;
    };
    {
        const state::ScopedAccountRuntime scope(devices[0].runtime);
        std::array<std::byte,7719> startup{};
        if(!check(late_private_startup_wire(identities[0],"edz_freeroam",30,startup),"solo svc6 wire fixture"))return false;
        middleware::bap::activity_host_manager::Request parsed{};
        middleware::bap::activity_host_manager::request::selection::StartupReservations nativeOwner{};
        if(!check(middleware::bap::activity_host_manager::request::parse_request(startup,parsed)
            && middleware::bap::activity_host_manager::request::selection::parse_startup_reservations(parsed.protobuf,nativeOwner)
            && nativeOwner.present && nativeOwner.members.count==1
            && nativeOwner.members.peers[0].accountSoid==identities[0].accountSoid,
            "native wire really contains exactly the authenticated solo owner"))return false;
        std::array<std::byte,137> reply{};std::size_t written{};bool allocated{};
        a::PendingAllocation allocation{};
        const auto revision=devices[0].runtime.activity->stateRevision;
        if(!check(!e::activity_host_manager::encode_response(startup,std::span(reply).first(136),
            written,allocation,allocated) && !allocated && !allocation.prepared && written==0
            && devices[0].runtime.activity->stateRevision==revision,
            "short svc7 buffer cannot allocate or publish startup owner"))return false;
        auto wrongOwner=identities[0];wrongOwner.opaqueSoid=identities[1].opaqueSoid;
        std::array<std::byte,7719> unauthorized{};
        if(!check(late_private_startup_wire(wrongOwner,"edz_freeroam",30,unauthorized)
            && e::activity_host_manager::encode_response(unauthorized,reply,written,allocation,allocated)
            && allocated && allocation.startupOwner.memberKey==0
            && devices[0].runtime.activity->stateRevision==revision,
            "foreign character cannot seed solo owner; legacy best-effort reply stays uncommitted"))return false;
        if(!check(e::activity_host_manager::encode_response(startup,reply,written,allocation,allocated)
            && allocated && written==137 && !allocation.socialWorld && allocation.publicLeaseParts==0,
            "production solo svc6 returns private svc7 allocation"))return false;
        if(!check(allocation.startupOwner.memberKey==identities[0].memberKey
            && allocation.startupOwner.accountSoid==identities[0].accountSoid
            && std::none_of(allocation.startupReservations.begin(),allocation.startupReservations.end(),
                [](const auto& row){return row.memberKey!=0;}),
            "production solo svc6 retains owner without fabricated peer reservations"))return false;
        session=enc::read_u64_be(std::span(reply).subspan<1,8>());
        e::ServiceOutcome outcome{};outcome.transaction=allocation;e::transactions::Publication publication{};
        if(!check(e::transactions::commit(outcome,publication) && publication.activitySessionId==session,
            "production startup commit publishes returned private ID"))return false;
        e::activity_message::ActivityPlan joined{};bool transaction{};
        if(!check(e::activity_message::process(session,message(identities[0]),joined,transaction) && transaction,
            "real solo owner type3 accepted after startup"))return false;
        outcome={};outcome.transaction=joined;
        a::membership::PendingMutation identity{};
        if(!check(e::transactions::commit(outcome,publication) && publication.activitySessionFromJoin
            && a::membership::prepare_identity(session,identities[0],identity) && a::membership::commit(identity),
            "production solo join and identity commit"))return false;
        a::SessionRecord host{};state::AccountRuntimeContext found{};
        if(!check(a::snapshot_for_runtime(devices[0].runtime,identities[0].accountSoid,session,host)
            && host.sharedLeaseParts==a::shared::kPrivateLeaseParts
            && a::shared::owner_context(session,found) && found.account==devices[0].runtime.account,
            "solo wire startup creates discoverable canonical owner before late join"))return false;
    }
    const state::ScopedAccountRuntime guest(devices[1].runtime);
    e::activity_message::ActivityPlan plan{};bool transaction{};
    const auto native=message(identities[1]);
    if(!check(e::activity_message::process(0,native,plan,transaction) && !transaction
        && !a::contains(session),"valid but nonmember identity cannot join"))return false;
    request={};request.operation=wire::Operation::joinLobby;request.lobbyId=lobby;
    const auto friends=+[](std::uint64_t left,std::uint64_t right) noexcept {
        return (left==0x01100001FF820001ULL && right==0x01100001FF820002ULL)
            || (right==0x01100001FF820001ULL && left==0x01100001FF820002ULL);
    };
    if(!check(realtime_store::execute(platforms[1],devices[1].accountSoid,now,request,friends,response)
        && response.result==1,"late member joins central fireteam"))return false;
    for(unsigned kind=0;kind<3;++kind) {
        auto invalid=identities[1];
        if(kind==0)invalid.accountSoid=identities[0].accountSoid;
        if(kind==1)invalid.opaqueSoid=identities[0].opaqueSoid;
        if(kind==2)invalid.joinIdentity=0;
        if(!check(e::activity_message::process(0,message(invalid),plan,transaction) && !transaction,
            "spoofed account, foreign character and missing incarnation refused"))return false;
    }
    if(!check(e::activity_message::process(0,native,plan,transaction) && transaction
        && plan.delivery==e::activity_message::Delivery::joinNotifications
        && plan.entitySlotMutation.importsShared && plan.entitySlotMutation.admitsPrivatePeer
        && plan.entitySlotMutation.sharedRecord.sharedMemberSlot==1 && plan.correlation==0x82ABCDEF,
        "full native type3 produces authorized private admission transaction"))return false;
    static server::bap::Scratch scratch{};
    std::array<std::byte,state::kAesKeySize> key{};
    std::array<std::byte,state::kBapNonceSize> nonce{},readerNonce{};
    std::array<std::byte,16> tiny{};std::size_t written{};
    if(!check(!e::push::activity::append_join_notifications(scratch,plan,key,nonce,tiny,written)
        && written==0 && nonce==readerNonce && !a::contains(session),"failed encoding cannot commit or consume nonce"))return false;
    static std::array<std::byte,65536> output{},plain{};
    if(!check(e::push::activity::append_join_notifications(scratch,plan,key,nonce,output,written)
        && !a::contains(session),"all native replies staged before state mutation"))return false;
    std::size_t offset=0;
    for(const auto expected:{4U,0U,1U,54U}) {
        middleware::bap::OuterFrame frame{};std::size_t clearSize{};
        if(!check(middleware::bap::parse_frame(std::span<const std::byte>(output).subspan(offset,written-offset),frame)
            && middleware::secure_channel::open_frame(key,readerNonce,frame.payload,plain,clearSize)
            && clearSize>=23 && enc::read_u16_be(std::span(plain).first<2>())==9
            && enc::read_u64_be(std::span(plain).subspan<7,8>())==session
            && enc::read_u32_be(std::span(plain).subspan<15,4>())==expected,
            "decrypt actual svc9 order: accepted4, lease0, global1, host54"))return false;
        if(expected==4) {
            auto payload=std::span<const std::byte>(plain).subspan(23,clearSize-23);
            enc::bits::Reader bits(payload);std::uint64_t accepted{};
            if(!check(enc::read_u32_be(payload.first<4>())==0x82ABCDEF
                && enc::read_u64_be(payload.subspan<4,8>())==session
                && bits.skip(570) && bits.read(3,accepted) && accepted==1,"native accepted result echoes exact request"))return false;
        }
        if(expected==0) {
            const auto& mask=plan.entitySlotMutation.mask;
            if(!check(clearSize==23+mask.size() && std::equal(mask.begin(),mask.end(),plain.begin()+23),
                "encrypted grant contains the exact disjoint lease"))return false;
        }
        offset+=6+frame.payload.size();middleware::secure_channel::advance_nonce(readerNonce);
    }
    if(!check(offset==written && readerNonce==nonce,"complete ordered replies; no extra frames"))return false;
    e::ServiceOutcome outcome{};outcome.transaction=plan;e::transactions::Publication publication{};
    if(!check(e::transactions::commit(outcome,publication) && publication.hasActivitySessionBinding
        && publication.activitySessionFromJoin && publication.activitySessionId==session && a::is_joined(session),
        "production commit publishes exact late-join connection binding"))return false;
    a::membership::PendingMutation identity{};
    if(!check(a::membership::prepare_identity(session,identities[1],identity) && a::membership::commit(identity),
        "native type23 identity accepted after join"))return false;
    {
        const state::ScopedAccountRuntime host(devices[0].runtime);a::membership::PendingMutation refresh{};
        if(!check(a::membership::prepare_refresh(session,0,-1,refresh)
            && shared_joined_notification_test(session,refresh,3),"host encrypted roster contains both actual joined identities"))return false;
    }
    a::shared::disconnect(devices[1].runtime,session);a::shared::disconnect(devices[0].runtime,session);
    if(!check(realtime_store::remove_lobby_member(lobby,platforms[1],platforms[1])
        && realtime_store::remove_lobby_member(lobby,platforms[0],platforms[0]),"fixture cleanup"))return false;
    std::puts("PASS: late private route: real account/lobby authorization, native type3, encrypted 4/0/1/54, exact lease, publication and type23");
    std::puts("PASS: solo startup through late join: real svc6/7 owner, native solo type3, authorized late type3 and encrypted replies");
    return true;
}
}
