#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include "middleware/bap/activity_message/client_authoritative_data.h"
#include "middleware/bap/activity_message/activity_join_request_parser.h"
#include "middleware/bap/activity_message/replicate_membership.h"
#include "state/activity/membership/transactions/internal.h"
namespace bits=dawn::middleware::encoding::bits;
namespace data=dawn::middleware::bap::activity_message::client_authoritative_data;
namespace wire=dawn::middleware::bap::activity_message::replicate_membership;
using Peer=dawn::core::protocol::ActivityPeerConnection;
void check(bool ok,const char* step){if(!ok){std::printf("FAIL peer connection: %s\n",step);std::exit(1);}}
void expect(bits::Reader& r,std::uint8_t width,std::uint64_t value){std::uint64_t actual{};check(r.read(width,actual)&&actual==value,"wire field");}
Peer peer(unsigned seed){
    Peer p{};p.hasFlags=p.hasProxy=p.hasPeer=p.hasAlternate=true;p.flags=0x10;
    for(unsigned i=0;i<86;++i){p.peer[i]=std::byte{static_cast<unsigned char>(seed+i)};p.proxy[i]=p.peer[i];p.alternate[i]=std::byte{static_cast<unsigned char>(seed+2*i)};}
    p.peer.back()=p.proxy.back()=std::byte{6};return p;
}
void b2(bits::Writer& w,const Peer& p,bool blob=true){
    check(w.write(1,1)&&w.write(p.flags,6)&&w.write(0,9),"B2 prefix");
    for(const auto* address:{&p.proxy,&p.peer,&p.alternate}){check(w.write(1,1),"address marker");for(auto b:*address)check(w.write(std::to_integer<unsigned>(b),8),"address byte");}
    check(w.write(0,1)&&w.write(blob?1U:0U,1),"name/blob markers");
    if(blob){check(w.write(3,14)&&w.write(0xA1B2C3,24),"bounded opaque blob");}
    check(w.write(0,1),"B2 tail");
}
int main(){
    std::array<std::byte,65536> output{};std::size_t size{};
    const Peer p=peer(17);
    bits::Writer w(output);check(w.write(1,1)&&w.write(1,1),"delta roots");b2(w,p);
    check(w.write(0,3)&&w.finish(size),"delta end");
    data::ClientAuthoritativeData decoded{};
    check(data::parse_client_authoritative_data(std::span(output).first(size),decoded)&&decoded.peerConnection==p,"complete typed delta");
    for(std::size_t end=0;end<size;++end){decoded.peerConnection=p;check(!data::parse_client_authoritative_data(std::span(output).first(end),decoded)&&decoded.peerConnection==Peer{},"truncated delta leaves no partial metadata");}
    bits::Writer join(output);check(join.write(7,32)&&join.write(0x100,64),"join prefix");
    for(unsigned i=0;i<8;++i)check(join.write(i+1,8),"member key");
    check(join.write(0,10)&&join.write(0x7fffffff,32)&&join.write(4,64)&&join.write(5,64)&&join.write(6,64)&&join.write(7,64),"join identity");
    b2(join,p);check(join.write(0,1)&&join.write(0,23)&&join.write(0,32)&&join.write(0,16)&&join.write(0,16)&&join.write(0,8)&&join.finish(size),"join remainder");
    dawn::middleware::bap::activity_message::JoinRequest j{};
    check(dawn::middleware::bap::activity_message::join_request::parse_join_request(std::span(output).first(size),j)&&j.peerConnection==p&&j.characterSoid==6,"join B2 begins at458, complete owned metadata");
    check(dawn::middleware::bap::activity_message::join_request::parse_join_request(std::span(output).first(20),j)&&j.peerConnection==Peer{},"legacy prefix-only join unchanged");
    namespace state=dawn::state::activity::membership;
    state::MembershipState stored{};stored.peerConnection=p;
    state::AuthoritativeUpdate sparse{};sparse.peerConnection.hasFlags=true;sparse.peerConnection.flags=0;
    const auto merged=state::transactions::merge(stored,sparse);
    check(merged.peerConnection.peer==p.peer&&merged.peerConnection.flags==0,"flags-only delta preserves original address; explicit zero retained");
    check(!state::transactions::equal_authoritative(stored,merged),"connection change advances publication");
    check(state::transactions::merge(merged,{}).peerConnection==merged.peerConnection,"absence is not a reset");
    auto guard=sparse;guard.peerConnection.peer[3]=std::byte{0xff};
    check(!state::transactions::equal(sparse,guard),"all metadata participates in transaction guard");
    for(std::uint8_t local:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{31}}){
        wire::MembershipSnapshot s{};s.localMemberSlot=local;s.joinedMemberMask=0xffffffff;s.reservationField3WireWordMask=0xffffffff;
        for(unsigned slot=0;slot<32;++slot){
            const dawn::middleware::bap::activity_message::client_identity::ClientIdentity id{1000+slot,-1,-1,2000+slot,3000+slot,4000+slot,5000+slot};
            s.peerConnections[slot]=peer(slot*3+1);
            if(slot==local)s.identity=id;else s.reservations[(slot==0?local:slot)-1]=id;
        }
        s.publishPeerConnections=true;s.citizen.present=true;s.citizen.memberKey=99999;
        check(wire::peer_view_mask(s)==(0xffffffffU&~(1U<<local)),"view mask contains only remote ready peers");
        check(wire::encode_replicate_membership(s,output,size)&&size==wire::encoded_size(s),"32-member packet exact dynamic size");
        bits::Reader r(std::span(output).first(size));check(r.skip(65),"member start");
        for(unsigned slot=0;slot<32;++slot){
            check(r.skip(363),"outer member identity");expect(r,1,1);expect(r,1,1);
            const bool remote=slot!=local;
            for(unsigned field=0;field<15;++field){
                const bool present=field==3||field==5||field==14||(remote&&(field==0||field==10||field==11||field==12));
                expect(r,1,present?1:0);
                if(field==0&&remote)expect(r,6,0x10);
                if(field==3)expect(r,64,3000+slot);if(field==5)expect(r,64,4000+slot);
                if(remote&&field>=10&&field<=12){const auto& a=field==10?s.peerConnections[slot].proxy:field==11?s.peerConnections[slot].peer:s.peerConnections[slot].alternate;for(auto b:a)expect(r,8,std::to_integer<unsigned>(b));}
                if(field==14){expect(r,14,18);check(r.skip(144),"identity blob unchanged");}
            }
            expect(r,1,0);expect(r,3,0);expect(r,1,1);expect(r,5,1);
        }
        expect(r,1,1);check(size*8-r.remaining_bits()==wire::region_block_start_bit(s),"region offset includes all connections");
        bits::Reader tail(std::span(output).first(size));check(tail.skip(wire::region_block_end_bit(s)),"mask offset");
        expect(tail,1,1);expect(tail,32,0xffffffff);expect(tail,1,1);expect(tail,32,0xffffffff);
        expect(tail,1,1);expect(tail,32,0xffffffffU&~(1U<<local));expect(tail,2,0);
        const auto before=output;std::size_t failed=99;
        check(!wire::encode_replicate_membership(s,std::span(output).first(size-1),failed)&&failed==0&&before==output,"short buffer untouched");
        const auto other=local==0?1U:0U;
        s.peerConnections[other].peer=s.peerConnections[local].peer;
        check(!wire::publishes_peer_connection(s,other),"duplicate self/remote endpoint never opens alias channel");
        s.peerConnections[other]=peer(201);s.peerConnections[other].peer.back()=std::byte{0};
        check(!wire::publishes_peer_connection(s,other),"current public server placeholders excluded");
        s.peerConnections[other]=peer(201);s.peerConnections[other].flags=0;
        check(wire::publishes_peer_connection(s,other)&&(wire::peer_view_mask(s)&(1U<<other))==0,"client zero readiness is not overridden");
        s.joinedMemberMask&=~(1U<<other);check(!wire::publishes_peer_connection(s,other),"unjoined reservation cannot open link");
    }
    std::puts("PASS: typed peer join/delta, every truncated prefix, sparse merge/guards, all32 slots, exact addresses, view masks, bounds and alias protection");
}
