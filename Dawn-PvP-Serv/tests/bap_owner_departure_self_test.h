#pragma once
#include "late_private_route_self_test.h"
#include "server/bap/runtime.h"

namespace restoration::tests {
inline bool bap_owner_departure_self_test(const dawn::state::session::Credentials& owner) noexcept {
    using namespace dawn;
    namespace a=state::activity;namespace b=middleware::bap;namespace c=middleware::secure_channel;
    namespace enc=middleware::encoding;namespace net=client::network;
    const auto check=[](bool ok,const char* why){if(!ok)std::printf("FAIL: BAP owner departure: %s\n",why);return ok;};
    std::array<server::bap::ConnectionStatus,server::bap::kSessionCount> statuses{};
    if(!check(server::bap::snapshot_connections(statuses)==0,"no live transports in isolated fixture"))return false;
    struct Cleanup {~Cleanup(){server::bap::shutdown();}} cleanup;
    if(!check(state::register_account_runtime_resolver(nullptr,&account_store::resolve_runtime_callback),
        "same account resolver as dedicated startup"))return false;
    struct Resolver {~Resolver(){state::unregister_account_runtime_resolver(nullptr,
        &account_store::resolve_runtime_callback);}} resolver;
    std::uint64_t ownerSoid{};
    const auto ownerRuntime=state::capture_account_runtime(ownerSoid);
    if(!check(ownerSoid==owner.accountSoid && ownerRuntime.account && ownerRuntime.activity,
        "default owner fixture identity"))return false;
    account_store::DeviceBinding device{};
    std::array<std::byte,core::identity::device_identity::kPublicKeySize> deviceKey{};
    deviceKey.front()=std::byte{0x84};deviceKey.back()=std::byte{0x35};
    constexpr std::uint64_t remotePlatform=0x01100001FF840001ULL;
    if(!check(account_store::load_or_create_device(remotePlatform,remotePlatform,deviceKey,device),
        "isolated remote device"))return false;
    device.runtime.activity->nextSessionId=0x8400;
    state::session::Credentials remote{};
    struct Wipe {state::session::Credentials& value;~Wipe(){SecureZeroMemory(&value,sizeof value);}} wipe{remote};
    if(!check(state::session::issue(device.accountSoid,remotePlatform,owner.signOn,
        static_cast<std::uint64_t>(std::time(nullptr)),remote),"remote SignOn"))return false;
    const std::array<const state::session::Credentials*,2> credentials{&owner,&remote};
    const std::array<state::AccountRuntimeContext,2> runtimes{ownerRuntime,device.runtime};
    std::array<std::array<std::byte,state::kBapNonceSize>,2> sendNonce{},replyNonce{};
    std::array<std::uint64_t,2> sessions{};
    std::array<std::uint64_t,2> lobbies{};
    static std::array<std::byte,65536> clear{},sealed{},frame{},reply{},opened{};
    struct Erase {~Erase(){SecureZeroMemory(clear.data(),clear.size());SecureZeroMemory(sealed.data(),sealed.size());
        SecureZeroMemory(frame.data(),frame.size());SecureZeroMemory(reply.data(),reply.size());
        SecureZeroMemory(opened.data(),opened.size());}} erase;
    const auto exchange=[&](std::size_t peer,std::uint16_t service,std::span<const std::byte> body,bool encrypted,
        std::uint16_t expectedService,std::size_t& bodySize) {
        clear.fill(std::byte{});enc::write_u16_be(std::span(clear).first<2>(),service);
        enc::write_u32_be(std::span(clear).subspan<2,4>(),1);
        std::copy(body.begin(),body.end(),clear.begin()+6);
        std::size_t sealedSize{},frameSize{};
        const auto payload=std::span<const std::byte>(clear).first(body.size()+6);
        if(encrypted) {
            if(!c::seal_frame(credentials[peer]->bap.sessionKey,sendNonce[peer],payload,sealed,sealedSize)
                || !b::encode_frame(b::FrameType::encrypted,std::span(sealed).first(sealedSize),frame,frameSize))return false;
        } else if(!b::encode_frame(b::FrameType::plaintext0,payload,frame,frameSize))return false;
        net::BapResponse response{};
        if(!server::bap::consume({net::BapEvent::frame,static_cast<std::uint32_t>(peer+1),
            std::span(frame).first(frameSize),reply},response))return false;
        if(encrypted)c::advance_nonce(sendNonce[peer]);
        bodySize=0;if(!expectedService)return response.size>0;
        b::OuterFrame parsed{};
        if(!b::parse_frame(std::span(reply).first(response.size),parsed))return false;
        std::span<const std::byte> result=parsed.payload;
        if(encrypted){std::size_t length{};
            if(!c::open_frame(credentials[peer]->bap.sessionKey,replyNonce[peer],parsed.payload,opened,length))return false;
            result=std::span(opened).first(length);c::advance_nonce(replyNonce[peer]);}
        if(result.size()<8 || enc::read_u16_be(result.first<2>())!=expectedService)return false;
        bodySize=result.size()-8;
        if(!encrypted)std::copy(result.begin(),result.end(),opened.begin());
        return true;
    };
    const auto identity=[&](std::size_t peer) {
        return a::membership::Identity{0xAF84000000000001ULL+peer,-1,-1,0xBB84000000000001ULL+peer,
            credentials[peer]->accountSoid,runtimes[peer].account->characters[0].soid,0};
    };
    const auto authenticate=[&](std::size_t peer) {
        net::BapResponse response{};
        if(!server::bap::consume({net::BapEvent::open,static_cast<std::uint32_t>(peer+1),{},{}},response))return false;
        std::array<std::byte,128> hello{};middleware::protobuf::Writer writer(hello);
        if(!writer.write_length_delimited(1,credentials[peer]->signOn.sessionToken) || !writer.write_varint(2,2))return false;
        std::size_t size{};
        if(!exchange(peer,25,std::span(hello).first(writer.size()),false,26,size))return false;
        sendNonce[peer]=credentials[peer]->bap.nonce;sendNonce[peer].back()^=std::byte{1};
        replyNonce[peer]=credentials[peer]->bap.nonce;
        return true;
    };
    const auto join=[&](std::size_t peer,std::uint64_t sessionId) {
        const auto id=identity(peer);
        std::array<std::byte,79> body{};auto out=std::span(body);
        enc::write_u64_be(out.first<8>(),sessionId);body[8]=std::byte{1};
        enc::write_u32_be(out.subspan<9,4>(),3);enc::write_u32_be(out.subspan<13,4>(),58);
        enc::bits::Writer bits(out.subspan(21));bool valid=true;
        const auto write=[&](std::uint64_t value,std::uint8_t width){valid=bits.write(value,width)&&valid;};
        write(0x84ABCDEF,32);write(sessionId,64);
        for(unsigned n=0;n<8;++n)write((id.memberKey>>(8*n))&255,8);
        write(0,10);write(0x7FFFFFFF,32);write(id.joinIdentity,64);write(id.accountSoid,64);
        write(id.opaqueSoid,64);write(0,64);write(0,6);std::size_t bitsSize{},size{};
        return valid && bits.finish(bitsSize) && bitsSize==58 && exchange(peer,8,body,true,0,size);
    };
    for(std::size_t peer=0;peer<2;++peer) {
        const auto& auth=*credentials[peer];
        const auto now=static_cast<std::uint64_t>(std::time(nullptr));
        middleware::social::realtime::Request lobbyRequest{};
        middleware::social::realtime::Response lobbyResponse{};
        lobbyRequest.operation=middleware::social::realtime::Operation::createLobby;
        lobbyRequest.valueA=2;lobbyRequest.valueB=6;
        if(!check(realtime_store::begin_game_session(auth.platformId,auth.accountSoid,auth.generation,now)
            && realtime_store::mark_gameplay_ready(auth.platformId,auth.accountSoid,auth.generation,now)
            && realtime_store::execute(auth.platformId,auth.accountSoid,now,lobbyRequest,nullptr,lobbyResponse)
            && lobbyResponse.result==1,"authenticated solo lobby for native reservation authorization"))return false;
        lobbies[peer]=lobbyResponse.lobby.id;
        if(!check(authenticate(peer),"real svc25 authentication"))return false;
        std::size_t size{};
        const auto id=identity(peer);
        std::array<std::byte,7719> startup{};
        if(!check(late_private_startup_wire(id,"edz_freeroam",30,startup)
            && exchange(peer,6,startup,true,7,size) && size==137,"encrypted solo allocation through BAP"))return false;
        sessions[peer]=enc::read_u64_be(std::span(opened).subspan<9,8>());
        if(!check(join(peer,sessions[peer]),"encrypted native type3 join through BAP"))return false;
        a::SessionRecord record{};
        if(!check(a::snapshot_for_runtime(runtimes[peer],id.accountSoid,sessions[peer],record)
            && record.joined && record.sharedOwnerAccountSoid==id.accountSoid,"real shared private activity joined"))return false;
    }
    net::BapResponse response{};
    {
        const state::ScopedAccountRuntime differentThreadAccount(device.runtime);
        if(!check(server::bap::consume({net::BapEvent::close,1,{},{}},response),"owner EOF"))return false;
    }
    a::SessionRecord record{};
    if(!check(!a::snapshot_for_runtime(ownerRuntime,ownerSoid,sessions[0],record),
        "default owner EOF retires its exact private activity"))return false;
    if(!check(a::snapshot_for_runtime(device.runtime,device.accountSoid,sessions[1],record) && record.joined,
        "owner EOF preserves the other authenticated account"))return false;
    if(!check(server::bap::consume({net::BapEvent::close,1,{},{}},response),"duplicate EOF harmless"))return false;
    const auto survivor=record;
    middleware::social::realtime::Request joinLobby{};middleware::social::realtime::Response lobbyReply{};
    joinLobby.operation=middleware::social::realtime::Operation::joinLobby;joinLobby.lobbyId=lobbies[1];
    const auto friends=+[](std::uint64_t,std::uint64_t) noexcept {return true;};
    if(!check(realtime_store::remove_lobby_member(lobbies[0],owner.platformId,owner.platformId)
        && realtime_store::execute(owner.platformId,ownerSoid,static_cast<std::uint64_t>(std::time(nullptr)),
            joinLobby,friends,lobbyReply) && lobbyReply.result==1
        && authenticate(0) && join(0,sessions[1]),"owner reauthenticates and joins the remote fireteam"))return false;
    lobbies[0]=lobbies[1];
    if(!check(a::snapshot_for_runtime(ownerRuntime,ownerSoid,sessions[1],record) && record.joined
        && record.sharedOwnerAccountSoid==device.accountSoid && record.sharedMemberSlot==1,
        "real native join imports the follower into the remote private activity"))return false;
    {
        const state::ScopedAccountRuntime differentThreadAccount(device.runtime);
        if(!check(server::bap::consume({net::BapEvent::close,1,{},{}},response),"follower EOF"))return false;
    }
    if(!check(!a::snapshot_for_runtime(ownerRuntime,ownerSoid,sessions[1],record)
        && a::snapshot_for_runtime(device.runtime,device.accountSoid,sessions[1],record) && record.joined
        && record.memberKey==survivor.memberKey && record.heldEntitySlots==survivor.heldEntitySlots,
        "follower EOF removes only the departed row and preserves the survivor lease"))return false;
    if(!check(server::bap::consume({net::BapEvent::close,2,{},{}},response)
        && !a::snapshot_for_runtime(device.runtime,device.accountSoid,sessions[1],record),
        "remote EOF also retires exact private activity"))return false;
    for(std::size_t peer=0;peer<2;++peer)
        if(!check(realtime_store::remove_lobby_member(lobbies[peer],credentials[peer]->platformId,
            credentials[peer]->platformId),"fixture lobby cleanup"))return false;
    std::puts("PASS: BAP owner departure: real owner/device svc25, encrypted svc6/7+type3, EOF cleanup and cross-account isolation");
    return true;
}
}
