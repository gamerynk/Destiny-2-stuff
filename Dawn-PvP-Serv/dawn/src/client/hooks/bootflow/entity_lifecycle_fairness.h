#pragma once
#include "entity_movement_fairness.h"
#include "combat_feedback_priority.h"

namespace dawn::client::hooks::bootflow::entity_lifecycle_fairness {
namespace memory=entity_movement_fairness;
constexpr std::size_t kBurst=4;
constexpr std::size_t kTreeLimit=64;
inline std::atomic<std::uintptr_t> imageBase{};
inline bool configure(std::uintptr_t image) noexcept {
    imageBase.store(0, std::memory_order_release);
    constexpr std::array<std::uint8_t,32> expected{
        0x66,0x83,0x4F,0x50,0x04,0xB8,0x80,0x00,0x00,0x00,0x66,0x09,0x42,0x50,
        0xB8,0x00,0x02,0x00,0x00,0x66,0x09,0x42,0x50,0xB8,0x00,0x04,0x00,0x00,
        0x66,0x09,0x42,0x50};
    std::array<std::uint8_t,32> actual{}; std::uintptr_t gather{};
    if (!image || !memory::read(reinterpret_cast<const void*>(image),0x1713F99,actual)
        || actual!=expected || !memory::read(reinterpret_cast<const void*>(image),0x1C9ADE8,gather)
        || gather!=image+0x170C080) return false;
    imageBase.store(image, std::memory_order_release);
    return true;
}
inline const std::byte* resolve(const std::byte* rows,const void* manager,
                               std::uint32_t token) noexcept {
    if(token==UINT32_MAX) return nullptr;
    const auto index=token&0x1FFFU;
    std::uint32_t live{},stored{};std::int16_t slot{};
    if(!memory::read(manager,0xC520+(index>>5)*4,live) || !(live&(1U<<(index&31)))
       || !memory::read(manager,0x114+index*6,slot) || slot<0 || slot>=1024) return nullptr;
    const auto* row=rows+static_cast<std::size_t>(slot)*0x70;
    return memory::read(row,8,stored) && stored==token ? row : nullptr;
}
inline bool removal(const std::byte* row,std::int32_t view,memory::Clock clock) noexcept {
    std::uint16_t global{},flags{};const std::byte* aux{};std::uint32_t views{};std::uint64_t stamp{};
    return memory::read(row,0x50,global) && (global&0x204U)==4U
        && memory::read(row,0x60,aux) && aux && memory::read(aux,4,views) && (views&(1U<<view))
        && memory::read(aux,0x18+static_cast<std::size_t>(view)*0x40+0x38,flags) && (flags&5U)==4U
        && memory::read(aux,0x18+static_cast<std::size_t>(view)*0x40+0x20,stamp)
        && clock.now>=stamp && clock.now-stamp>=50;
}
inline bool descendant_removal(const std::byte* rows,const void* manager,const std::byte* root,
                               std::uint32_t rootToken,std::int32_t view,memory::Clock clock,
                               std::size_t* budget=nullptr) noexcept {
    struct Link {std::uint32_t token,parent;};
    std::array<Link,kTreeLimit> pending{};
    std::array<std::uint32_t,kTreeLimit> visited{};
    std::size_t size=0,seen=0;bool found=false;std::uint32_t first{};
    if(!memory::read(root,0x48,first)) return false;
    if(first!=UINT32_MAX) pending[size++]={first,rootToken};
    while(size) {
        if(budget) {if(!*budget) return false;--*budget;}
        if(seen==kTreeLimit) return false;
        const auto link=pending[--size];
        if(link.token==rootToken) return false;
        for(std::size_t i=0;i<seen;++i) if(visited[i]==link.token) return false;
        visited[seen++]=link.token;
        const auto* row=resolve(rows,manager,link.token);
        std::uint32_t parent{},child{},sibling{};std::uint8_t kind{},owner{};
        if(!row || !memory::read(row,0,kind) || kind!=0 || !memory::read(row,1,owner) || owner!=0xFF
           || !memory::read(row,0xC,parent) || parent!=link.parent
           || !memory::read(row,0x44,sibling) || !memory::read(row,0x48,child)) return false;
        found=removal(row,view,clock) || found;
        if(sibling!=UINT32_MAX) {if(size==kTreeLimit) return false;pending[size++]={sibling,parent};}
        if(child!=UINT32_MAX) {if(size==kTreeLimit) return false;pending[size++]={child,link.token};}
    }
    return found;
}
inline bool eligible(void* lane,std::uint32_t packed,memory::Clock clock,
                     std::uint32_t channel=1,std::size_t* budget=nullptr) noexcept {
    const auto* rows=memory::table.load(std::memory_order_acquire);
    const auto image=imageBase.load(std::memory_order_acquire);
    const auto index=packed&0x7FFFU;
    std::uintptr_t vtable{};const void* manager{};std::int32_t view{};
    std::uint8_t enabledA{},enabledB{};
    if(!rows || !image || !clock.valid || channel>2 || index>0x1FFF || ((packed>>29)&3U)!=channel
       || ((packed>>27)&3U)!=2 || !memory::read(lane,0,vtable) || vtable!=image+0x1C9ADD8
       || !memory::read(lane,9,enabledA) || !enabledA
       || !memory::read(lane,10,enabledB) || !enabledB
       || !memory::read(lane,0x10,manager) || !manager
       || !memory::read(lane,0xC,view) || view<0 || view>=32) return false;
    std::int16_t slot{};
    if(!memory::read(manager,0x114+index*6,slot) || slot<0 || slot>=1024) return false;
    const auto* row=rows+static_cast<std::size_t>(slot)*0x70;
    std::uint8_t kind{},owner{};std::uint32_t token{},views{},parent{};
    std::uint16_t globalFlags{},flags{};const std::byte* aux{};std::uint64_t stamp{};
    if(!memory::read(row,0,kind) || kind!=0 || !memory::read(row,1,owner) || owner!=0xFF
       || !memory::read(row,8,token) || token==UINT32_MAX || (token&0x1FFFU)!=index
       || resolve(rows,manager,token)!=row || !memory::read(row,0xC,parent) || parent!=UINT32_MAX
       || !memory::read(row,0x50,globalFlags) || (globalFlags&0x200U)
       || !memory::read(row,0x60,aux) || !aux || !memory::read(aux,4,views)
       || !(views&(1U<<view))) return false;
    const auto* descriptor=aux+0x18+static_cast<std::size_t>(view)*0x40;
    // descriptor bit2 is the removal wire operation, independent of the snapshot dirty mask; bit7
    // confirms dependency eligibility. Creation (bit0) stays native
    if(!memory::read(descriptor,0x38,flags) || (flags&0x81U)!=0x80U
       || !memory::read(descriptor,0x20,stamp) || clock.now<stamp
       || clock.now-stamp<50) return false;
    if(removal(row,view,clock)) return true;
    return (flags&0x200U) && descendant_removal(rows,manager,row,token,view,clock,budget);
}
inline std::size_t apply(void* lane,std::uint32_t view,std::uint32_t laneIndex,
                  std::int32_t capacity,std::int32_t count,std::uint32_t* output,
                  memory::Clock clock) noexcept {
    if(view>2 || laneIndex!=2 || !lane || !output || capacity<0 || capacity>2000
       || count<0 || count>capacity) return 0;
    std::size_t raised=0,budget=256;
    for(std::int32_t i=0;i<count && raised<kBurst && budget;++i) {
        std::uint32_t packed{};
        if(memory::read(output,static_cast<std::size_t>(i)*4,packed)
           && (packed&memory::kPriorityMask)!=memory::kPriorityMask
           && eligible(lane,packed,clock,view,&budget) && combat_feedback_priority::promote(output+i,packed)) ++raised;
    }
    return raised;
}
}
