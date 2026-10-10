#include <array>
#include <cstdio>
#include <cstdlib>
#include "middleware/bap/activity_message/replicate_membership.h"
#include "middleware/encoding/bit_reader.h"
namespace wire=dawn::middleware::bap::activity_message::replicate_membership;
namespace bits=dawn::middleware::encoding::bits;
void check(bool ok,const char* step) {if(!ok){std::printf("FAIL: shared wire: %s\n",step);std::exit(1);}}
std::uint64_t read_key(bits::Reader& reader) {
    std::uint64_t result=0,value{};
    for(unsigned i=0;i<8;++i){check(reader.read(8,value),"key byte");result|=value<<(i*8);}
    return result;
}
int main() {
    std::array<std::byte,8192> encoded{};
    for (std::uint8_t local:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{31}}) {
        wire::MembershipSnapshot s{};
        s.identity={0x1122334455667788ULL,-1,-1,0x1020304050607080ULL,
            0x8000100100100100ULL,0x8000100100100102ULL,0x1234567890ABCDEFULL};
        s.localMemberSlot=local;
        const auto other=local==0?1:0;
        auto host=s.identity;host.memberKey=0x8877665544332211ULL;host.accountSoid+=0x100;host.field5+=0x100;
        s.reservations[local==0?0:local-1]=host;
        s.hostMemberKey=local==0?s.identity.memberKey:host.memberKey;
        s.hasMemberTransitionTokens=true;
        s.reservationField3WireWordMask=0xFFFFFFFFU;
        for(std::size_t i=0;i<32;++i)s.memberTransitionTokens[i]=static_cast<std::uint8_t>(i+1);
        std::size_t size{};
        check(wire::encode_replicate_membership(s,encoded,size),"encode");
        bits::Reader reader(std::span(encoded).first(size));
        check(reader.skip(65),"header");
        for(unsigned slot=0;slot<32;++slot) {
            std::uint64_t present{},detail{},leave{},reason{};
            check(reader.read(1,present),"member presence");
            check(present==unsigned(slot==local || slot==unsigned(other)),"canonical occupancy");
            if(present) {
                check(read_key(reader)==(slot==local?s.identity.memberKey:host.memberKey),"distinct canonical key");
                std::uint64_t nonce{};
                check(reader.skip(42)&&reader.read(64,nonce)
                    &&nonce==(slot==local?s.identity.field3:host.field3)&&reader.skip(192),
                    "type23 opaque field3 wire bytes survive moving to another member slot");
            }
            check(reader.read(1,detail)&&detail==unsigned(slot==local),"only actual local blob, no invented remote readiness");
            if(detail)check(reader.skip(306),"local detail");
            check(reader.read(1,leave),"leave presence");
            if(leave)check(reader.read(5,reason)&&reason==1,"local not booted");
        }
        bits::Reader tokens(std::span(encoded).first(size));
        check(tokens.skip(wire::region_block_start_bit(s)+123),"token table");
        for(unsigned slot=0;slot<32;++slot){std::uint64_t token{};check(tokens.read(8,token)&&token==slot+1,"per-member token lane");}
        bits::Reader tail(std::span(encoded).first(size));
        check(tail.skip(wire::region_block_start_bit(s)+64*451+134),"host tail");
        check(read_key(tail)==s.hostMemberKey,"one shared host authority");
        bits::Reader masks(std::span(encoded).first(size));
        std::uint64_t presence{},joined{},occupied{};
        check(masks.skip(wire::region_block_end_bit(s))&&masks.read(1,presence)&&presence==1
            &&masks.read(32,joined)&&joined==(std::uint64_t{1}<<local)
            &&masks.read(1,presence)&&presence==1&&masks.read(32,occupied)
            &&occupied==((std::uint64_t{1}<<local)|(std::uint64_t{1}<<other)),"separate creation and occupancy masks");
        const auto before=encoded;
        std::size_t rejected=10;
        check(!wire::encode_replicate_membership(s,std::span(encoded).first(size-1),rejected)
            &&rejected==0&&encoded==before,"undersized output untouched");
    }
    std::puts("PASS: shared wire canonical slots0/1/31, keys, host authority, per-member tokens, masks, bounds and no fabricated remote detail");
}
