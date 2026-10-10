#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
#include "middleware/gameplay/peer/packet_fragments.h"
namespace wire=dawn::middleware::gameplay::peer::fragments;
unsigned checks{};
void require(bool value){++checks;if(!value){std::fprintf(stderr,"failed %u\n",checks);std::exit(1);}}
std::vector<std::byte> part(const std::vector<std::byte>& packet,unsigned sequence,unsigned guard,
                           unsigned count,unsigned index,unsigned stride){
    const auto begin=static_cast<std::size_t>(index)*stride;
    const auto end=(std::min)(packet.size(),begin+stride);
    std::vector<std::byte> bytes{std::byte(0x40U|sequence),std::byte((guard<<6)|((count-1)<<3)|index)};
    bytes.insert(bytes.end(),packet.begin()+begin,packet.begin()+end);return bytes;
}
int main(){
    // native prefix 0x7848: sequence 56, guard 1, two parts, index 0
    wire::Prefix prefix{};const std::array golden{std::byte{0x78},std::byte{0x48},std::byte{0x10}};
    require(wire::read_prefix(golden,prefix)&&prefix.sequence==56&&prefix.guard==1&&prefix.count==2&&prefix.index==0);
    auto assembly=std::make_unique<wire::Assembly>();
    for(unsigned stride:{1226U,1238U})for(unsigned count=1;count<=8;++count){
        std::vector<std::byte> packet((count-1)*stride+19);
        for(std::size_t i=0;i<packet.size();++i)packet[i]=std::byte((i*37U)&255U);
        packet[0]=std::byte{0x10};
        for(unsigned rotation=0;rotation<count;++rotation){
            require(assembly->reset(17,1,static_cast<std::uint16_t>(stride)));
            for(unsigned i=0;i<count;++i){
                const unsigned index=(count-1-i+rotation)%count;
                const auto bytes=part(packet,56,1,count,index,stride);
                auto receipt=assembly->receive(17,bytes,100+i);
                require(receipt.result==(i+1==count?wire::Result::complete:wire::Result::pending));
                if(i+1==count)require(std::equal(packet.begin(),packet.end(),receipt.packet.begin(),receipt.packet.end()));
                receipt=assembly->receive(17,bytes,100+i);
                require(receipt.result==wire::Result::duplicate);
                require(receipt.packet.empty()==(i+1!=count));
            }
        }
    }
    std::vector<std::byte> packet(1239,std::byte{0xA5});packet[0]=std::byte{0x10};
    const auto first=part(packet,63,1,2,0,1238),last=part(packet,63,1,2,1,1238);
    require(assembly->reset(18,1,1238));
    require(assembly->receive(17,first,100).result==wire::Result::refused);
    require(assembly->receive(18,first,100).result==wire::Result::pending);
    auto bad=first;bad.back()^=std::byte{1};
    require(assembly->receive(18,bad,101).result==wire::Result::conflict);
    require(assembly->receive(18,last,102).result==wire::Result::conflict);
    require(assembly->receive(18,first,2100).result==wire::Result::conflict);
    require(assembly->receive(18,last,2101).result==wire::Result::pending);
    require(assembly->receive(18,first,2102).result==wire::Result::complete);
    require(assembly->receive(18,first,2101).result==wire::Result::refused);
    require(assembly->reset(19,1,1238));
    for(unsigned sequence=0;sequence<8;++sequence)
        require(assembly->receive(19,part(packet,sequence,1,2,0,1238),100).result==wire::Result::pending);
    require(assembly->receive(19,part(packet,8,1,2,0,1238),100).result==wire::Result::capacity);
    require(assembly->receive(19,part(packet,8,1,2,0,1238),2101).result==wire::Result::pending);
    require(assembly->reset(20,1,1238));
    auto shortFirst=first;shortFirst.pop_back();
    require(assembly->receive(20,shortFirst,1).result==wire::Result::refused);
    auto oversized=first;oversized.push_back(std::byte{});
    require(assembly->receive(20,oversized,1).result==wire::Result::refused);
    auto wrongGuard=first;wrongGuard[1]=std::byte{8};
    require(assembly->receive(20,wrongGuard,1).result==wire::Result::refused);
    require(assembly->receive(20,first,1).result==wire::Result::pending);
    auto changedCount=first;changedCount[1]=std::byte{0x50};
    require(assembly->receive(20,changedCount,2).result==wire::Result::conflict);
    require(assembly->reset(21,1,1238));
    auto wrongInner=first;wrongInner[2]=std::byte{0};
    require(assembly->receive(21,wrongInner,1).result==wire::Result::pending);
    require(assembly->receive(21,last,2).result==wire::Result::conflict);
    require(!assembly->reset(0,1,1238));
    require(!assembly->reset(22,4,1238));
    require(!assembly->reset(22,1,0));
    require(!assembly->reset(22,1,1239));
    std::printf("packet_fragments_test PASS checks=%u\n",checks);
}
