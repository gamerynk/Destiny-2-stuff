#pragma once
#include "social_primary_admission_self_test.h"
#include "server/bap/encrypted/activity_host_manager/activity_host_manager_route.h"
#include "server/bap/encrypted/internal.h"
#include "server/bap/encrypted/transactions/service_outcome_commit.h"
#include "middleware/encoding/bit_writer.h"
#include "middleware/encoding/byte_order.h"
#include "middleware/protobuf/codec.h"

namespace restoration::tests {
inline bool social_activity_route_self_test() noexcept {
    using namespace dawn;
    namespace a=state::activity;
    namespace e=server::bap::encrypted;
    namespace realtime=middleware::social::realtime;
    const auto check=[](bool ok,const char* label){if(!ok)std::printf("FAIL: social route: %s\n",label);return ok;};
    std::array<account_store::DeviceBinding,2> devices{};
    constexpr std::array<std::uint64_t,2> platforms{0x01100001FFB00001ULL,0x01100001FFB00002ULL};
    const auto now=static_cast<std::uint64_t>(std::time(nullptr));
    std::uint64_t common{};
    for(std::size_t i=0;i<2;++i) {
        std::array<std::byte,core::identity::device_identity::kPublicKeySize> key{};
        key.front()=std::byte{0xB9};key.back()=std::byte{static_cast<unsigned char>(i+1)};
        if(!check(account_store::load_or_create_device(platforms[i],platforms[i],key,devices[i]),"isolated enrolled device"))return false;
        const auto& device=devices[i];
        const state::ScopedAccountRuntime scope(device.runtime);
        if(!realtime_store::begin_game_session(platforms[i],device.accountSoid,1,now)
            || !realtime_store::mark_gameplay_ready(platforms[i],device.accountSoid,1,now))return false;
        realtime::Request lobbyRequest{};realtime::Response lobbyResponse{};
        lobbyRequest.operation=realtime::Operation::createLobby;lobbyRequest.valueA=2;lobbyRequest.valueB=3;
        if(!check(realtime_store::execute(platforms[i],device.accountSoid,now,lobbyRequest,nullptr,lobbyResponse)
            && lobbyResponse.result==1,"independent central lobby"))return false;
        std::array<std::byte,6000> snapshot{};middleware::encoding::bits::Writer bits(snapshot);
        bool valid=true;
        const auto write=[&](std::uint64_t value,std::uint8_t width){valid=bits.write(value,width)&&valid;};
        const auto zeros=[&](std::size_t count){while(count>64){write(0,64);count-=64;}write(0,static_cast<std::uint8_t>(count));};
        const auto keyBytes=[&](std::uint64_t value){for(unsigned b=0;b<8;++b)write((value>>(8*b))&255,8);};
        write(0,10);zeros(2048+2048+8192+256+4+1+8+4+32+2);
        write(1,4);write(1,12);write(21,12);zeros(3+5+8+1);
        write(1,1);write(0xAB28899E,32);write(1,1);write(0x2EA8FB98,32);write(1,1);
        constexpr std::string_view package="city_tower_social_d2";
        for(std::size_t c=0;c<40;++c)write(128+(c<package.size()?package[c]:0),8);
        zeros(4);zeros((256+1024)*8);write(1,6);
        const auto member=0xAA69000000000001ULL+i;
        keyBytes(member);write(0,10);write(0x7FFFFFFF,32);keyBytes(0xBA69000000000001ULL+i);
        const auto character=device.runtime.account->characters[0].soid;
        write(device.accountSoid,64);write(character,64);write(0,64);zeros(64+64+32+10);
        std::size_t snapshotSize{};if(!check(valid && bits.finish(snapshotSize),"startup wire fixture"))return false;
        std::array<std::byte,7719> request{};request[0]=std::byte{3};
        middleware::protobuf::Writer protobuf(std::span(request).subspan(5));
        if(!protobuf.write_length_delimited(2,std::span(snapshot).first(snapshotSize)))return false;
        for(unsigned b=0;b<4;++b)request[1+b]=std::byte{static_cast<unsigned char>((protobuf.size()>>(b*8))&255)};
        std::array<std::byte,137> response{};a::PendingAllocation allocation{};std::size_t written{};bool hasAllocation{};
        if(!check(e::activity_host_manager::encode_response(request,response,written,allocation,hasAllocation)
            && hasAllocation && allocation.socialWorld && written==137,"authenticated svc6 -> exact svc7"))return false;
        const auto chosen=middleware::encoding::read_u64_be(std::span(response).subspan<1,8>());
        if(i==0)common=chosen;
        if(!check(chosen==common && (i==0 || allocation.socialSessionId==common),"same primary session in response"))return false;
        e::ServiceOutcome outcome{};outcome.transaction=allocation;e::transactions::Publication publication{};
        if(!check(e::transactions::commit(outcome,publication) && publication.hasActivitySessionBinding
            && publication.activitySessionId==chosen,"connection publication uses response ID, not tentative private ID"))return false;
        if(!check(a::shared::social_join_authorized(common,member,character),"wire identity admitted for exact account"))return false;
        if(!realtime_store::remove_lobby_member(lobbyResponse.lobby.id,platforms[i],platforms[i]))return false;
    }
    a::shared::disconnect(devices[0].runtime,common);
    std::puts("PASS: social svc6 wire decode, authenticated solo startup, shared svc7 and connection publication");
    return true;
}
}
