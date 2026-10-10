#pragma once

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include "internal.h"
#include "entity_movement_fairness.h"
#include "remote_facing_policy.h"

#ifndef RESTORATION_REMOTE_FACING_TRACE
#define RESTORATION_REMOTE_FACING_TRACE 0
#endif

namespace dawn::client::hooks::bootflow::remote_facing_diagnostic {
namespace policy = remote_facing_policy;
namespace memory = entity_movement_fairness;
#if RESTORATION_REMOTE_FACING_TRACE
inline SRWLOCK lock = SRWLOCK_INIT;
inline bool enabled{};
inline std::uint64_t generation{};
inline std::uint32_t remaining{};
inline policy::Key selected{};
inline thread_local bool observing{};

inline bool same_identity(const policy::Key& a,const policy::Key& b) noexcept {
    return a.component==b.component && a.asset==b.asset && a.owner==b.owner && a.legacy==b.legacy;
}
inline void start() noexcept {
    AcquireSRWLockExclusive(&lock);
    if (!enabled) { enabled=true; ++generation; remaining=32; selected={}; }
    ReleaseSRWLockExclusive(&lock);
}
inline void stop() noexcept {
    AcquireSRWLockExclusive(&lock); enabled=false; remaining=0; selected={}; ++generation;
    ReleaseSRWLockExclusive(&lock);
}
struct Source {
    std::uint32_t handle{},asset{},type{},owner{},legacy{};
    std::uintptr_t address{};
    bool identity{};
};

inline Source source_identity(std::uintptr_t image,std::uint32_t handle) noexcept {
    Source out{}; out.handle=handle;
    if (!image || !handle || handle>=0x80000000U) return out;
    const void* header{}; const std::byte* table{}; const std::byte* rows{};
    const auto shifted=handle>>13;
    const auto bank=(((shifted|0x0FFC0000U)>>18)&(shifted&0xFFFFU));
    if (bank>=0x4000 || !memory::read(reinterpret_cast<const void*>(image+0x2439C70),0,header)
        || !memory::read(header,0,table) || !table) return out;
    const auto* descriptor=table+static_cast<std::size_t>(bank)*0x40;
    std::uint32_t stride{}; std::int32_t mask{}; std::uint64_t relative{};
    if (!memory::read(descriptor,8,rows) || !rows || !memory::read(descriptor,0x30,stride)
        || stride<0x30 || stride>0x100000 || !memory::read(descriptor,0x34,mask)) return out;
    const auto raw=reinterpret_cast<std::uintptr_t>(rows)+static_cast<std::size_t>(handle&0x1FFFU)*stride;
    if (!memory::read(reinterpret_cast<const void*>(raw),8,relative)) return out;
    const auto address=raw-(relative&static_cast<std::uint64_t>(static_cast<std::int64_t>(mask)));
    if (address<0x10000 || address>=0x7FFFFFFFFFFFULL || address%16) return out;
    const auto* root=reinterpret_cast<const void*>(address);
    std::int64_t parent{};
    if (!memory::read(root,0,out.asset) || !memory::read(root,4,out.type)
        || !memory::read(root,0x10,parent) || parent!=0
        || !memory::read(root,0x24,out.owner) || out.owner!=handle
        || !memory::read(root,0x2C,out.legacy)) return out;
    const void* finalHeader{}; const std::byte* finalTable{}; const std::byte* finalRows{};
    std::uint32_t finalStride{},finalOwner{},finalAsset{},finalType{},finalLegacy{};
    std::int64_t finalParent{};
    std::int32_t finalMask{}; std::uint64_t finalRelative{};
    if (!memory::read(reinterpret_cast<const void*>(image+0x2439C70),0,finalHeader) || finalHeader!=header
        || !memory::read(header,0,finalTable) || finalTable!=table
        || !memory::read(descriptor,8,finalRows) || finalRows!=rows
        || !memory::read(descriptor,0x30,finalStride) || finalStride!=stride
        || !memory::read(descriptor,0x34,finalMask) || finalMask!=mask
        || !memory::read(reinterpret_cast<const void*>(raw),8,finalRelative) || finalRelative!=relative
        || !memory::read(root,0x24,finalOwner) || finalOwner!=out.owner
        || !memory::read(root,0,finalAsset) || finalAsset!=out.asset
        || !memory::read(root,4,finalType) || finalType!=out.type
        || !memory::read(root,0x10,finalParent) || finalParent!=parent
        || !memory::read(root,0x2C,finalLegacy) || finalLegacy!=out.legacy) return out;
    out.address=address; out.identity=true; return out;
}

struct Sample {
    policy::Key key{}; policy::State state{};
    std::array<std::uint32_t,2> sources{},dependencies{};
    std::array<std::int8_t,2> roles{};
    std::uint32_t dependencyCount{};
    std::int32_t packetEpoch{};
    std::uint8_t packetFlags{},ready{},ac6{},ac7{},acc{};
    std::array<Source,4> roots{};
};
template<class Identify>
inline bool capture(std::uintptr_t image,const void* component,Identify identify,Sample& out) noexcept {
    out={};
    if (!identify(component,out.key,out.state) || !policy::supported(out.state)) return false;
    if (!memory::read(component,0x97C,out.sources[0]) || !memory::read(component,0x984,out.sources[1])
        || !memory::read(component,0x980,out.roles[0]) || !memory::read(component,0x988,out.roles[1])
        || !memory::read(component,0xA5C,out.dependencies[0]) || !memory::read(component,0xA60,out.dependencies[1])
        || !memory::read(component,0x98C,out.dependencyCount) || !memory::read(component,0x3F0,out.packetFlags)
        || !memory::read(component,0x3F4,out.packetEpoch) || !memory::read(component,0x400,out.ready)
        || !memory::read(component,0xAC6,out.ac6) || !memory::read(component,0xAC7,out.ac7)
        || !memory::read(component,0xACC,out.acc)) return false;
    const std::array<std::uint32_t,4> handles{out.sources[0],out.sources[1],out.dependencies[0],out.dependencies[1]};
    for (std::size_t i=0;i<handles.size();++i) {
        bool repeated{};
        for (std::size_t j=0;j<i;++j) if (handles[i]==handles[j]) { out.roots[i]=out.roots[j]; repeated=true; break; }
        if (!repeated) out.roots[i]=source_identity(image,handles[i]);
    }
    policy::Key finalKey{}; policy::State finalState{};
    return identify(component,finalKey,finalState) && same_identity(out.key,finalKey)
        && finalKey.controlEpoch==out.key.controlEpoch && policy::supported(finalState);
}

template<class Identify>
inline void observe(std::uintptr_t image,const void* component,std::string_view stage,Identify identify,
                    bool maySelect=false) noexcept {
    const auto error=GetLastError();
    if (observing) { SetLastError(error); return; }
    std::uint64_t epoch{}; policy::Key target{}; bool admitted{};
    AcquireSRWLockShared(&lock);
    admitted=enabled && remaining && (selected.component || maySelect);
    epoch=generation; target=selected;
    ReleaseSRWLockShared(&lock);
    if (!admitted) { SetLastError(error); return; }
    observing=true;
    if (!component) component=reinterpret_cast<const void*>(target.component);
    Sample sample{};
    if (capture(image,component,identify,sample)) {
        std::uint32_t ordinal{};
        AcquireSRWLockExclusive(&lock);
        if (enabled && generation==epoch && remaining
            && ((!selected.component && maySelect) || same_identity(selected,sample.key))) {
            if (!selected.component) selected=sample.key;
            ordinal=33-remaining; --remaining;
        }
        ReleaseSRWLockExclusive(&lock);
        if (ordinal) {
            std::array<char,2048> text{};
            const auto& s=sample.state;
            const auto n=std::snprintf(text.data(),text.size(),
                "ev=remote_facing_diagnostic stage=%.*s sample=%u generation=%llu tid=%lu"
                " selection=first_supported_remote_candidate component=0x%llX owner=0x%08X legacy=0x%08X"
                " asset=0x%08X class=0x80803A00 identity=qualified snapshot=best_effort"
                " source0=0x%08X role0=%d source1=0x%08X role1=%d dep0=0x%08X dep1=0x%08X dep_count=%u dep_rows=unobserved"
                " packet_flags=%u packet_epoch=%d ready=%u received_epoch=%u native_clock=unobserved future_packet=unqualified"
                " ac6=%u ac7=%u acc=%u control_flags=%u"
                " received=%.9g,%.9g,%.9g control=%.9g,%.9g,%.9g secondary=%.9g,%.9g,%.9g"
                " facing=%.9g,%.9g,%.9g secondary_facing=%.9g,%.9g,%.9g"
                " root0=%u:0x%llX:0x%08X:0x%08X:0x%08X:0x%08X"
                " root1=%u:0x%llX:0x%08X:0x%08X:0x%08X:0x%08X"
                " root2=%u:0x%llX:0x%08X:0x%08X:0x%08X:0x%08X"
                " root3=%u:0x%llX:0x%08X:0x%08X:0x%08X:0x%08X roots=identity,address,asset,type,owner,legacy",
                static_cast<int>(stage.size()),stage.data(),ordinal,static_cast<unsigned long long>(epoch),GetCurrentThreadId(),
                static_cast<unsigned long long>(sample.key.component),sample.key.owner,sample.key.legacy,sample.key.asset,
                sample.sources[0],static_cast<int>(sample.roles[0]),sample.sources[1],static_cast<int>(sample.roles[1]),
                sample.dependencies[0],sample.dependencies[1],sample.dependencyCount,sample.packetFlags,sample.packetEpoch,
                sample.ready,sample.key.controlEpoch,sample.ac6,sample.ac7,sample.acc,s.flags,
                s.received.x,s.received.y,s.received.z,s.control.x,s.control.y,s.control.z,s.secondary.x,s.secondary.y,s.secondary.z,
                s.facing.x,s.facing.y,s.facing.z,s.secondaryFacing.x,s.secondaryFacing.y,s.secondaryFacing.z,
                sample.roots[0].identity,static_cast<unsigned long long>(sample.roots[0].address),sample.roots[0].asset,sample.roots[0].type,sample.roots[0].owner,sample.roots[0].legacy,
                sample.roots[1].identity,static_cast<unsigned long long>(sample.roots[1].address),sample.roots[1].asset,sample.roots[1].type,sample.roots[1].owner,sample.roots[1].legacy,
                sample.roots[2].identity,static_cast<unsigned long long>(sample.roots[2].address),sample.roots[2].asset,sample.roots[2].type,sample.roots[2].owner,sample.roots[2].legacy,
                sample.roots[3].identity,static_cast<unsigned long long>(sample.roots[3].address),sample.roots[3].asset,sample.roots[3].type,sample.roots[3].owner,sample.roots[3].legacy);
            if (n>0 && static_cast<std::size_t>(n)<text.size())
                core::log::write(core::log::Channel::client,core::log::Level::info,{text.data(),static_cast<std::size_t>(n)});
        }
    }
    observing=false; SetLastError(error);
}
#else
inline void start() noexcept {}
inline void stop() noexcept {}
template<class Identify>
inline void observe(std::uintptr_t,const void*,std::string_view,Identify,bool=false) noexcept {}
#endif
} // namespace dawn::client::hooks::bootflow::remote_facing_diagnostic
