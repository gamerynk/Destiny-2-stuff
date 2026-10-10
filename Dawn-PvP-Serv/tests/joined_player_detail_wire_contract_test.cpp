#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include "middleware/bap/activity_message/replicate_membership.h"
#include "middleware/encoding/bit_reader.h"
namespace wire=dawn::middleware::bap::activity_message::replicate_membership;
namespace bits=dawn::middleware::encoding::bits;
using Identity=dawn::middleware::bap::activity_message::client_identity::ClientIdentity;
void check(bool ok,const char* step) {if(!ok){std::printf("FAIL: joined detail: %s\n",step);std::exit(1);}}
void expect(bits::Reader& r,std::uint8_t width,std::uint64_t value,const char* step) {
    std::uint64_t actual{};check(r.read(width,actual)&&actual==value,step);
}
void key(bits::Reader& r,std::uint64_t value) {
    for(unsigned i=0;i<8;++i)expect(r,8,(value>>(i*8))&255,"canonical member key");
}
void detail(bits::Reader& r,const Identity& identity) {
    expect(r,1,1,"nested detail presence");
    for(unsigned field=0;field<15;++field) {
        expect(r,1,field==3||field==5||field==14?1:0,"identity field presence");
        if(field==3)expect(r,64,identity.accountSoid,"distinct nested account");
        if(field==5)expect(r,64,identity.field5,"distinct nested character");
        if(field==14) {
            expect(r,14,18,"minimal player blob length");
            expect(r,3,1,"blob root");expect(r,1,0,"blob flag");
            expect(r,10,0,"blob index");expect(r,1,1,"blob identity presence");
            expect(r,64,identity.accountSoid,"distinct blob account");
            expect(r,64,identity.field5,"distinct blob character");expect(r,1,0,"blob pad");
        }
    }
    expect(r,1,0,"identity extension absent");expect(r,3,0,"detail trailing flags");
}
int main() {
    std::array<std::byte,8192> output{},baseline{};
    for(std::uint8_t local:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{31}}) {
        for(bool citizen:{false,true}) for(bool allJoined:{false,true}) {
            wire::MembershipSnapshot s{};
            std::array<Identity,32> identities{};
            for(unsigned slot=0;slot<32;++slot) {
                identities[slot]={0x1122334455667700ULL+slot,-1,-1,
                    0x0123456789ABC000ULL+slot,0x8000100100100100ULL+(slot<<12),
                    0x8000100100100102ULL+(slot<<12),0xFEDCBA9876540000ULL+slot};
            }
            s.identity=identities[local];s.localMemberSlot=local;
            for(unsigned slot=0;slot<32;++slot) {
                if(slot==local)continue;
                const auto source=slot==0?local:slot;
                s.reservations[source-1]=identities[slot];
            }
            s.reservationField3WireWordMask=0xFFFFFFFFU;
            s.hostMemberKey=identities[0].memberKey;
            s.hasMemberTransitionTokens=true;
            for(unsigned slot=0;slot<32;++slot)s.memberTransitionTokens[slot]=static_cast<std::uint8_t>(slot+1);
            s.citizen.present=citizen;s.citizen.memberKey=0xABCDEF0123456789ULL;
            std::size_t baselineSize{};
            check(wire::encode_replicate_membership(s,baseline,baselineSize),"legacy reservations encode");
            s.joinedMemberMask=std::uint32_t{1}<<local;
            std::size_t size{};
            check(wire::encode_replicate_membership(s,output,size)&&size==baselineSize
                &&std::equal(output.begin(),output.begin()+size,baseline.begin()),"explicit self bit is byte-identical");
            const unsigned other=local==0?1:0;
            s.joinedMemberMask=allJoined?0xFFFFFFFFU:((std::uint32_t{1}<<local)|(std::uint32_t{1}<<other));
            check(wire::remote_detail_count(s)==(allJoined?31U:1U),"remote detail count excludes self");
            check(wire::encode_replicate_membership(s,output,size)&&size==wire::encoded_size(s),"shared detail encode");
            bits::Reader r(std::span(output).first(size));check(r.skip(65),"header");
            for(unsigned slot=0;slot<32;++slot) {
                const auto& identity=identities[slot];
                const bool joined=(s.joinedMemberMask&(std::uint32_t{1}<<slot))!=0;
                expect(r,1,1,"occupied identity");key(r,identity.memberKey);
                expect(r,10,0,"usable native field1 minus one");expect(r,32,0x7FFFFFFF,"field2");
                expect(r,64,identity.field3,"type23 incarnation wire order");
                expect(r,64,identity.accountSoid,"outer account");expect(r,64,identity.field5,"outer character");
                expect(r,64,identity.field6,"opaque field6");expect(r,1,joined?1:0,"joined detail only");
                if(joined)detail(r,identity);
                expect(r,1,joined?1:0,"leave field only for detail rows");
                if(joined)expect(r,5,1,"logical zero leave reason");
            }
            expect(r,1,1,"region block presence at calculated offset");
            check(size*8-r.remaining_bits()==wire::region_block_start_bit(s),"exact post-detail region offset");
            bits::Reader masks(std::span(output).first(size));
            check(masks.skip(wire::region_block_end_bit(s)),"mask offset includes all detail and citizen bits");
            expect(masks,1,1,"joined mask presence");expect(masks,32,s.joinedMemberMask,"all and only confirmed joined slots");
            expect(masks,1,1,"occupied mask presence");expect(masks,32,0xFFFFFFFFU,"reservations still occupied");
            expect(masks,3,0,"message tail");
            bits::Reader tokens(std::span(output).first(size));
            check(tokens.skip(wire::region_block_start_bit(s)+123),"token offset");
            for(unsigned slot=0;slot<32;++slot)expect(tokens,8,slot+1,"per-member transition lane unchanged");
            const auto before=output;std::size_t rejected=10;
            check(!wire::encode_replicate_membership(s,std::span(output).first(size-1),rejected)
                &&rejected==0&&before==output,"short output unchanged");
            auto invalid=s;invalid.reservationField3WireWordMask&=~(std::uint32_t{1}<<other);
            check(!wire::encode_replicate_membership(invalid,output,rejected)&&rejected==0&&before==output,
                "startup key representation cannot masquerade as validated joined detail");
            invalid=s;invalid.localMemberSlot=32;
            check(!wire::encode_replicate_membership(invalid,output,rejected)&&rejected==0&&before==output,
                "out-of-range local slot rejected before any write or shift");
            invalid=s;invalid.reservations[(other==0?local:other)-1]={};
            check(!wire::encode_replicate_membership(invalid,output,rejected)&&rejected==0&&before==output,
                "joined mask cannot reference an absent member");
        }
    }
    std::puts("PASS: joined detail locals0/1/31, distinct account/character/blob/incarnation, 2 or32 joined, reservations excluded, citizen offsets, self compatibility and rejection bounds");
}
