#pragma once
#include <cmath>
#include "entity_movement_fairness.h"

namespace dawn::client::hooks::bootflow::remote_weapon_native_wake {
namespace memory=entity_movement_fairness;
using Wake=void(__fastcall*)(void*,const void*,std::uint8_t) noexcept;
inline std::atomic<std::uintptr_t> imageBase{};
inline std::atomic<Wake> nativeWake{};
struct Request {void* root{};const void* binding{};std::uint32_t owner{},legacy{},asset{};bool authority{};float budget{};};

inline bool resolve(std::uintptr_t image,std::uint32_t datum,std::uintptr_t& output) noexcept {
    if(!image || !datum || datum==UINT32_MAX) return false;
    const void* root{};const std::byte* table{};const void* rows{};
    const auto shifted=static_cast<std::uint32_t>(static_cast<std::int32_t>(datum)>>13);
    const auto bank=(((shifted|0x0FFC0000U)>>18)&(shifted&0xFFFFU));
    std::uint32_t stride{};std::int32_t mask{};std::uint64_t relative{};
    if(!memory::read(reinterpret_cast<const void*>(image+0x2439C70),0,root)
       || !memory::read(root,0,table) || !table) return false;
    const auto* descriptor=table+static_cast<std::size_t>(bank)*0x40;
    if(!memory::read(descriptor,8,rows) || !rows
       || !memory::read(descriptor,0x30,stride) || stride<16 || stride>0x100000
       || !memory::read(descriptor,0x34,mask)) return false;
    const auto raw=reinterpret_cast<std::uintptr_t>(rows)+static_cast<std::size_t>(datum&0x1FFFU)*stride;
    if(!memory::read(reinterpret_cast<const void*>(raw),8,relative)) return false;
    output=raw-(relative&static_cast<std::uint64_t>(static_cast<std::int64_t>(mask)));
    return output>=0x10000 && output<0x7FFFFFFFFFFFULL;
}

inline bool prepare(const void* component,std::uintptr_t image,Request& output,bool afterShot=false) noexcept {
    if(!component || !image || reinterpret_cast<std::uintptr_t>(component)%16) return false;
    std::uint32_t asset{},type{},owner{},parentOwner{},legacy{},frameAsset{},frameType{},selectedOwner{};
    std::uint64_t parentOffset{},frameOffset{},selectedOffset{};
    std::int64_t rootParent{};
    std::uint8_t phase{},mask{};float timer{};
    if(!memory::read(component,0,asset) || asset<0x80000000U || asset==UINT32_MAX
       || !memory::read(component,4,type) || type!=0x80803865U
       || !memory::read(component,0x98,owner) || !owner || owner>=0x80000000U
       || !memory::read(component,0xA0,parentOffset) || parentOffset!=0
       || !memory::read(component,0x16F9,phase) || phase>4
       || !memory::read(component,0x16FC,timer) || !std::isfinite(timer) || timer<0 || timer>10) return false;
    std::uintptr_t address{};
    if(!resolve(image,owner,address) || address+0xD70!=reinterpret_cast<std::uintptr_t>(component)) return false;
    auto* root=reinterpret_cast<std::byte*>(address);
    if(!memory::read(root,0,frameAsset) || frameAsset!=asset
       || !memory::read(root,0x10,rootParent) || rootParent!=0
       || !memory::read(root,0x24,parentOwner) || parentOwner!=owner
       || !memory::read(root,0x2C,legacy) || legacy==UINT32_MAX
       || !memory::read(root,0x7B0,mask) || !(mask&8U)
       || !memory::read(root,0x860,selectedOwner) || selectedOwner!=owner
       || !memory::read(root,0x868,selectedOffset) || selectedOffset!=0xD70
       || !memory::read(root,0x6D0,frameAsset) || frameAsset!=asset
       || !memory::read(root,0x6D4,frameType) || frameType!=0x80803953U
       || !memory::read(root,0x6D8,frameOffset) || frameOffset<0x10 || frameOffset>0x200000) return false;
    const auto index=legacy&0x1FFFU;std::uint32_t authority{},suppressed{};const void* suppression{};
    if(!memory::read(reinterpret_cast<const void*>(image+0x26BE0E0),(index>>5)*4,authority)
       || !memory::read(reinterpret_cast<const void*>(image+0x1FBA9C8),0,suppression)
       || !memory::read(suppression,(index>>5)*4,suppressed)
       || (suppressed&(1U<<(index&31)))) return false;
    const bool local=(authority&(1U<<(index&31)))!=0;
    // local authoritative weapons already get their native update; the render-frame binding would
    // advance the cooldown twice (SUROS capture: 100 ms timers expiring in about 50 ms)
    (void)afterShot;
    if(local || phase!=2 || timer<=0) return false;
    std::uintptr_t templateBase{};
    std::array<std::byte,16> binding{};
    if(!resolve(image,asset,templateBase)
       || templateBase+frameOffset+0x20>=0x7FFFFFFFFFFFULL
       || !memory::read(reinterpret_cast<const void*>(templateBase+frameOffset+0x10),0,binding)) return false;
    output={root,reinterpret_cast<const void*>(templateBase+frameOffset+0x10),owner,legacy,asset,false,0};
    return true;
}
inline bool request(const void* component,bool afterShot=false) noexcept {
    const auto wake=nativeWake.load(std::memory_order_acquire);
    const auto image=imageBase.load(std::memory_order_acquire);
    if(!wake || !image) return false;
    const auto error=GetLastError();
    Request target{};
    const bool prepared=prepare(component,image,target,afterShot);
    if(prepared) {
        wake(target.root,target.binding,1);
    }
    SetLastError(error);
    return prepared;
}
inline void configure(std::uintptr_t image) noexcept {
    nativeWake=nullptr;imageBase=0;
    const std::array<std::uint8_t,17> wakeEntry{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x50,0x0F,0x10,0x01,0x41,0x0F,0xB6,0xF8};
    const std::array<std::uint8_t,15> nativeCall{0x48,0x8B,0xC8,0x48,0x8D,0x53,0x10,0x41,0xB0,0x01,0xE8,0x03,0x54,0x76,0xFF};
    std::array<std::uint8_t,17> actual{};std::array<std::uint8_t,15> call{};std::uintptr_t callback{};
    const bool supported=image && memory::read(reinterpret_cast<const void*>(image+0x44E3C0),0,actual) && actual==wakeEntry
        && memory::read(reinterpret_cast<const void*>(image+0xCE8FAE),0,call) && call==nativeCall
        && memory::read(reinterpret_cast<const void*>(image+0x1D04130),0,callback) && callback==image+0xD0BF80;
    if(supported) {imageBase=image;nativeWake=reinterpret_cast<Wake>(image+0x44E3C0);}
    core::log::write(core::log::Channel::client,core::log::Level::info,supported
        ? "ev=remote_weapon_native_wake stage=install result=ok status=candidate"
        : "ev=remote_weapon_native_wake stage=install result=disabled reason=unsupported_native_binding");
}
inline void disable() noexcept {nativeWake=nullptr;imageBase=0;}
}
