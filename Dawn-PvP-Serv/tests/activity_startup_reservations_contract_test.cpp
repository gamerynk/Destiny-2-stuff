#include <array>
#include <cstdio>
#include <cstdlib>
#include "middleware/bap/activity_host_manager/request/selection/activity_manager_reservations.h"
#include "middleware/encoding/bit_writer.h"
#include "middleware/protobuf/codec.h"

using namespace dawn::middleware;
namespace startup=bap::activity_host_manager::request::selection;
static void require(bool value) { if(!value) { std::puts("FAIL: startup reservation codec"); std::exit(1); } }
static void zeros(encoding::bits::Writer& w, std::size_t n) {
    while(n>64) {require(w.write(0,64)); n-=64;}
    require(w.write(0,static_cast<std::uint8_t>(n)));
}
static void key(encoding::bits::Writer& w,std::uint64_t k) {
    for(unsigned i=0;i<8;++i) require(w.write((k>>(8*i))&255,8));
}
static std::size_t fixture(std::span<std::byte> out,unsigned count,unsigned tokenCount) {
    encoding::bits::Writer w(out);
    require(w.write(tokenCount,10)); zeros(w,tokenCount*8);
    zeros(w,2048+2048+8192+256+4+1+8+4+32+2);
    zeros(w,52); // minimal 80808716: all optional fields absent, both variable counts zero
    zeros(w,(256+1024)*8);
    require(w.write(count,6));
    for(unsigned i=0;i<count && i<32;++i) {
        key(w,0x1020304050607000ULL+i);
        require(w.write(0,10) && w.write(0x7fffffff,32));
        key(w,0x8070605040302000ULL+i);
        require(w.write(0x1000100ULL+i*0x100,64)
            && w.write(0x1000101ULL+i*0x100,64) && w.write(0x550000ULL+i,64));
    }
    zeros(w,64+64+32+10);
    std::size_t size{}; require(w.finish(size)); return size;
}
static std::size_t wrap(std::span<const std::byte> snapshot,std::span<std::byte> out,bool twice=false) {
    protobuf::Writer w(out);
    require(w.write_length_delimited(2,snapshot));
    if(twice) require(w.write_length_delimited(2,snapshot));
    return w.size();
}
int main() {
    std::array<std::byte,6000> snapshot{};
    std::array<std::byte,13000> body{};
    startup::StartupReservations result{};
    require(startup::parse_startup_reservations({},result) && !result.present);
    auto emptySize=wrap({},body);
    require(startup::parse_startup_reservations(std::span(body).first(emptySize),result)
        && !result.present && result.members.count==0);
    const std::array<std::byte,2> wrongType{std::byte{0x10},std::byte{0}};
    require(!startup::parse_startup_reservations(wrongType,result));
    for(unsigned tokens: {0U,1U,568U}) for(unsigned count=0;count<=32;++count) {
        const auto n=fixture(snapshot,count,tokens);
        require(n==(23073+tokens*8+count*362+7)/8);
        const auto size=wrap(std::span(snapshot).first(n),body);
        require(startup::parse_startup_reservations(std::span(body).first(size),result)
            && result.present && result.members.count==count);
        for(unsigned i=0;i<count;++i) {
            const auto& p=result.members.peers[i];
            require(p.memberKey==0x1020304050607000ULL+i && p.field1==-1 && p.field2==-1
                && p.field3==0x8070605040302000ULL+i && p.accountSoid==0x1000100ULL+i*0x100
                && p.field5==0x1000101ULL+i*0x100 && p.field6==0x550000ULL+i);
        }
    }
    const auto n=fixture(snapshot,2,0);
    for(std::size_t i=1;i<n;++i) {
        const auto size=wrap(std::span(snapshot).first(i),body);
        require(!startup::parse_startup_reservations(std::span(body).first(size),result)
            && !result.present && result.members.count==0);
    }
    auto size=wrap(std::span(snapshot).first(n),body,true);
    require(!startup::parse_startup_reservations(std::span(body).first(size),result));
    size=wrap(std::span(snapshot).first(n+1),body);
    require(!startup::parse_startup_reservations(std::span(body).first(size),result));
    auto bad=fixture(snapshot,63,0); size=wrap(std::span(snapshot).first(bad),body);
    require(!startup::parse_startup_reservations(std::span(body).first(size),result));
    bad=fixture(snapshot,2,569); size=wrap(std::span(snapshot).first(bad),body);
    require(!startup::parse_startup_reservations(std::span(body).first(size),result));
    std::puts("PASS: startup field2, 0..32 peers, token bounds, identity endian, truncation and duplicate carrier");
}
