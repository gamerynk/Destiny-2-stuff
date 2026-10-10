#include <array>
#include <cstdio>
#include <cstring>
#include "middleware/encoding/bit_raw.h"
#include "middleware/gameplay/peer/join_messages.h"
#include "server/gameplay/group/activity_endpoint_identity.h"
namespace bits=dawn::middleware::encoding::bits;
namespace wire=dawn::middleware::gameplay::peer;
namespace binding=dawn::server::gameplay::group::activity_endpoint_identity;

bool check(bool value,const char* name) {
    if (!value) std::printf("FAIL: public native join: %s\n",name);
    return value;
}
// layout transcribed from retail 86657 reader RVA 16DD260
bool encode(bits::Writer& w,const wire::JoinMembers& m) {
    if (!w.write(0xA4F8,16) || !w.write(86657,32) || !w.write(86657,32) || !w.write(5,3)
        || !bits::write_raw_u64(w,0x1122334455667788ULL)
        || !bits::write_raw_u64(w,0x8877665544332211ULL) || !w.write(m.peerCount,5)) return false;
    for(std::size_t i=0;i<m.peerCount;++i) {
        const auto& p=m.peers[i];const auto method=static_cast<unsigned>(p.address.back());
        if (!w.write(method,3) || !bits::write_raw(w,std::span(p.address).first(method>=6?85:41))
            || !bits::write_raw_u64(w,p.opaquePeerId) || !w.write(p.opaquePeerField,16)
            || !w.write(p.playerIndex>=0?1:0,1)
            || (p.playerIndex>=0 && !w.write(static_cast<unsigned>(p.playerIndex),5))) return false;
    }
    if (!w.write(m.playerCount,5)) return false;
    for(std::size_t i=0;i<m.playerCount;++i) {
        const auto& p=m.players[i];
        if (!bits::write_raw_u64(w,p.playerId) || !bits::write_raw(w,p.identity)) return false;
        for(unsigned flag=0;flag<4;++flag)
            if (!w.write((p.flags>>(3-flag))&1U,1)) return false;
    }
    return true;
}
int main() {
    bool ok=true;
    wire::JoinMembers members{};
    members.peerCount=2;members.playerCount=2;
    for(std::size_t i=0;i<2;++i) {
        auto& peer=members.peers[i];auto& player=members.players[i];
        peer.address[0]=std::byte{127};peer.address[4]=static_cast<std::byte>(i+1);
        peer.address[5]=std::byte{17};peer.address.back()=std::byte{0};
        peer.opaquePeerId=0x123456789ABCDEF0ULL+i;peer.opaquePeerField=0xFFFF;
        peer.playerIndex=static_cast<std::int32_t>(1-i);
        player.playerId=0x1122334455667788ULL+i;
        const std::uint64_t platform=100+i;
        std::memcpy(player.identity.data(),&platform,8);
        player.flags=static_cast<std::uint8_t>(i+3);
    }
    std::array<std::byte,10000> bytes{};
    bits::Writer writer(bytes);std::size_t written{};
    ok=check(encode(writer,members)&&writer.finish(written),"encode fixture") && ok;
    const auto bitCount=writer.bit_count();
    wire::JoinRequest prefix{};wire::JoinMembers decoded{};
    bits::Reader reader(bytes,bitCount);
    ok=check(wire::read_join_request(reader,prefix)&&wire::read_join_members(reader,decoded)
        && reader.remaining_bits()==0 && decoded.peerCount==2 && decoded.playerCount==2,
        "parse complete native tables") && ok;
    ok=check(decoded.peers[0].opaquePeerId==members.peers[0].opaquePeerId
        && decoded.peers[0].opaquePeerField==0xFFFF && decoded.players[1].flags==4,
        "raw words and four native flags") && ok;
    const auto* selected=binding::select_player(decoded,members.peers[0].address);
    ok=check(selected && selected->playerId==members.players[1].playerId
        && binding::platform(*selected)==101,"explicit nonzero peer player index") && ok;
    auto unknown=members.peers[0].address;unknown[4]=std::byte{3};
    ok=check(!binding::select_player(decoded,unknown),"same IP wrong port rejected") && ok;
    decoded.peers[1].address=decoded.peers[0].address;
    ok=check(!binding::select_player(decoded,members.peers[0].address),"ambiguous full address rejected") && ok;
    for(std::size_t length=211;ok && length<bitCount;++length) {
        bits::Reader shortReader(bytes,length);wire::JoinMembers sentinel{};sentinel.peerCount=17;
        ok=check(wire::read_join_request(shortReader,prefix)
            && !wire::read_join_members(shortReader,sentinel) && sentinel.peerCount==17,
            "truncation is atomic") && ok;
    }
    members.peers[0].playerIndex=31;
    bits::Writer invalidWriter(bytes);written=0;
    ok=check(encode(invalidWriter,members)&&invalidWriter.finish(written),"invalid fixture") && ok;
    bits::Reader invalidReader(bytes,invalidWriter.bit_count());decoded.peerCount=19;
    ok=check(wire::read_join_request(invalidReader,prefix)
        && !wire::read_join_members(invalidReader,decoded) && decoded.peerCount==19,
        "out-of-table index rejected") && ok;
    members.peerCount=31;members.playerCount=31;
    for(std::size_t i=0;i<31;++i) {
        members.peers[i].address[4]=static_cast<std::byte>(i);
        members.peers[i].address.back()=std::byte{7};
        members.peers[i].address[84]=std::byte{55};
        members.peers[i].playerIndex=static_cast<std::int32_t>(i);
        members.players[i].playerId=i+1;
    }
    bits::Writer maximumWriter(bytes);written=0;
    ok=check(encode(maximumWriter,members)&&maximumWriter.finish(written),"maximum fixture") && ok;
    bits::Reader maximumReader(bytes,maximumWriter.bit_count());
    ok=check(wire::read_join_request(maximumReader,prefix)&&wire::read_join_members(maximumReader,decoded)
        && decoded.peerCount==31 && decoded.peers[30].address[84]==std::byte{55}
        && binding::select_player(decoded,members.peers[30].address)==&decoded.players[30],
        "31-entry relay tables") && ok;
    std::array<binding::Participant,3> participants{{
        {100,0x1122334455667788ULL,1000,2000,true},
        {101,0x1122334455667789ULL,1001,2001,true},{}}};
    wire::JoinPlayer claim{};claim.playerId=participants[1].playerId;
    std::memcpy(claim.identity.data(),&participants[1].platformId,8);
    ok=check(binding::resolve_participant(participants,claim)==1,"separate authenticated participant") && ok;
    claim.playerId=participants[0].playerId;
    ok=check(binding::resolve_participant(participants,claim)==3,"other member incarnation rejected") && ok;
    claim.playerId=participants[1].playerId;participants[1].current=false;
    ok=check(binding::resolve_participant(participants,claim)==3,"departed/unverified participant rejected") && ok;
    participants[1].current=true;participants[2]=participants[1];
    ok=check(binding::resolve_participant(participants,claim)==3,"ambiguous participant rejected") && ok;
    participants[2]={};participants[1].playerId++;
    ok=check(binding::resolve_participant(participants,claim)==3,"stale player incarnation rejected") && ok;
    std::puts(ok?"PASS: native ID10 tables, address/index mapping, 31-slot bounds, truncation, authenticated identity correlation"
                :"FAIL: native public join identity");
    return ok?0:1;
}
