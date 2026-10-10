#include <array>
#include <cstdio>
#include <cstring>
#include "middleware/bap/activity_message/replicate_membership.h"
#if defined(RESERVATION_CANDIDATE)
#include "middleware/bap/activity_message/peer_reservation.h"
#endif
namespace wire=dawn::middleware::bap::activity_message::replicate_membership;
namespace bits=dawn::middleware::encoding::bits;
bool check(bool value,const char* name) { if(!value) std::printf("FAIL: %s\n",name);return value; }
wire::MembershipSnapshot baseline(bool citizen) {
    wire::MembershipSnapshot s{};
    s.identity={0x1122334455667788ULL,-1,-1,0x1020304050607080ULL,0x9EAA000100100100ULL,
                0x9EAA000100100102ULL,0x8090A0B0C0D0E0F0ULL};
    s.revision=7;s.epoch=0;s.transitionToken=1;s.teleport.sliceSetIndex=-1;
    if(citizen) {
        s.citizen.present=true;s.citizen.memberKey=0xAABBCCDDEEFF0011ULL;
        s.citizen.onlineSessionId=0x9EAA000100200002ULL;s.citizen.regionIndex=408;
        s.citizen.ambassadorSlot=1;
        for(std::size_t i=0;i<s.citizen.descriptor.size();++i) s.citizen.descriptor[i]=std::byte(i);
    }
    return s;
}
int main() {
    std::array<std::byte,8192> out{};
    std::size_t size{};
    for(bool citizen:{false,true}) {
        auto s=baseline(citizen);
        if(!check(wire::encode_replicate_membership(s,out,size),"baseline encode")) return 1;
        std::uint64_t hash=14695981039346656037ULL;
        for(std::size_t i=0;i<size;++i) hash=(hash^std::to_integer<unsigned char>(out[i]))*1099511628211ULL;
        std::printf("BASELINE citizen=%u size=%zu fingerprint=%016llX\n",unsigned(citizen),size,
                    static_cast<unsigned long long>(hash));
    }
#if defined(RESERVATION_CANDIDATE)
    namespace reservation=dawn::middleware::bap::activity_message::peer_reservation;
    auto s=baseline(false);
    auto peer=s.identity;peer.memberKey=0xFFEEDDCCBBAA9988ULL;
    peer.accountSoid+=0x100;peer.field5+=0x100;peer.field3=0x8877665544332211ULL;
    // reference 80808697: epoch 32, count 6, unpadded BC identity 362 bits; one peer is 400 bits
    bits::Writer writer(out);
    auto key=[&](std::uint64_t v){for(unsigned i=0;i<8;++i)if(!writer.write((v>>(8*i))&255,8))return false;return true;};
    if(!check(writer.write(0,32)&&writer.write(1,6)&&key(peer.memberKey)&&writer.write(0,10)
        &&writer.write(0x7FFFFFFF,32)&&key(peer.field3)&&writer.write(peer.accountSoid,64)
        &&writer.write(peer.field5,64)&&writer.write(peer.field6,64)&&writer.finish(size)
        &&size==50,"reference reservation vector"))return 1;
    reservation::Request request{};
    if(!check(reservation::parse(std::span(out).first(size),request)&&request.count==1
        &&request.peers[0].memberKey==peer.memberKey&&request.peers[0].field3==peer.field3
        &&request.peers[0].field1==-1&&request.peers[0].field2==-1,"key endian and signed fields"))return 1;
    for(std::size_t n=0;n<size;++n)
        if(!check(!reservation::parse(std::span(out).first(n),request),"every truncation rejected"))return 1;
    out[4]=std::byte{0xFC};
    if(!check(!reservation::parse(std::span(out).first(size),request),"count63 rejected"))return 1;
    for(std::size_t n=1;n<=31;++n) {
        peer.memberKey=0x1000000000000000ULL+n;s.reservations[n-1]=peer;
        if(!check(wire::encode_replicate_membership(s,out,size)
            &&size==(wire::kMeaningfulBitCount+n*362+7)/8,"bounded multi reservation size"))return 1;
        bits::Reader reader(std::span(out).first(size));
        std::uint64_t present{},joined{},members{};
        if(!check(reader.skip(wire::region_block_end_bit(s))&&reader.read(1,present)&&present==1
            &&reader.read(32,joined)&&joined==1&&reader.read(1,present)&&present==1
            &&reader.read(32,members)&&members==(0xFFFFFFFFU>>(31-n)),
            "native lookup mask includes reservations; joined mask unchanged"))return 1;
    }
    s.reservations[1].memberKey=s.reservations[0].memberKey;
    out.fill(std::byte{0xA5});size=55;
    if(!check(!wire::encode_replicate_membership(s,out,size)&&size==0
        &&out[0]==std::byte{0xA5},"duplicate keys fail without output"))return 1;
    std::puts("PASS: reservation wire bounds, endian, truncation, 31 slots and joined-mask isolation");
#endif
    return 0;
}
