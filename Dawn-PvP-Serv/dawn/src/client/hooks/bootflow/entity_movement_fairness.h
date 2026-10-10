#pragma once

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "../../../core/logging/log.h"
#include "entity_movement_policy.h"

namespace dawn::client::hooks::bootflow::entity_movement_fairness {
using entity_movement_policy::Clock;
using entity_movement_policy::Key;
using entity_movement_policy::Policy;
using entity_movement_policy::kPriorityMask;
inline Policy policy{};
inline SRWLOCK lock=SRWLOCK_INIT;
inline std::atomic<const std::byte*> table{};
inline std::atomic<const std::byte*> clockRoot{};
inline std::atomic<const std::byte*> sobjectPool{};

struct Observation {
    std::uint64_t gathers{}, candidates{}, eligible{}, overdue{}, boosted{};
    std::uint64_t stampAdvances{}, advancesAfterBoost{}, nativeAgeSamples{}, maxNativeAgeMs{};
    std::uint64_t oldestStamp{};
    std::uint32_t oldestToken{}, oldestPeer{}, oldestPriority{}, channel{};
};
inline Observation observation{};
inline std::uint64_t lastReport{};

inline void report(const Observation& sample) noexcept {
    std::array<char, 640> line{};
    const int written = std::snprintf(
        line.data(), line.size(),
        "ev=entity_fairness stage=gather gathers=%llu candidates=%llu eligible=%llu"
        " overdue=%llu boosted=%llu send_stamp_advances=%llu advances_after_boost=%llu"
        " native_age_samples=%llu max_pending_age_ms=%llu oldest_token=0x%08X"
        " oldest_peer=%u oldest_stamp=%llu oldest_priority=0x%08X channel=%u mutation=priority_only",
        static_cast<unsigned long long>(sample.gathers),
        static_cast<unsigned long long>(sample.candidates),
        static_cast<unsigned long long>(sample.eligible),
        static_cast<unsigned long long>(sample.overdue),
        static_cast<unsigned long long>(sample.boosted),
        static_cast<unsigned long long>(sample.stampAdvances),
        static_cast<unsigned long long>(sample.advancesAfterBoost),
        static_cast<unsigned long long>(sample.nativeAgeSamples),
        static_cast<unsigned long long>(sample.maxNativeAgeMs),
        sample.oldestToken, sample.oldestPeer,
        static_cast<unsigned long long>(sample.oldestStamp), sample.oldestPriority, sample.channel);
    if (written > 0) {
        const auto length = static_cast<std::size_t>(written);
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         {line.data(), length < line.size() ? length : line.size() - 1U});
    }
}

template<class T> bool read(const void* base,std::size_t offset,T& value) noexcept {
    __try {
        if(!base) return false;
        value=*reinterpret_cast<const T*>(static_cast<const std::byte*>(base)+offset);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline Clock read_clock() noexcept {
    const auto root=clockRoot.load(std::memory_order_acquire);
    std::uint8_t initialized{}; std::uint64_t now{};
    if(!read(root,0,initialized) || initialized!=1 || !read(root,8,now)) return {};
    return {true,now};
}
inline bool configure_clock(const std::byte* helper,std::uintptr_t image) noexcept {
    clockRoot.store(nullptr,std::memory_order_release);
    constexpr std::array<std::uint8_t,6> start{0x40,0x53,0x48,0x83,0xEC,0x20};
    constexpr std::array<std::uint8_t,3> subtract{0x48,0x2B,0xC3};
    for(std::size_t i=0;i<start.size();++i) {std::uint8_t b{};if(!read(helper,i,b) || b!=start[i]) return false;}
    for(std::size_t i=0;i<subtract.size();++i) {std::uint8_t b{};if(!read(helper,0x1E+i,b) || b!=subtract[i]) return false;}
    std::uint16_t cmp{};std::uint8_t zero{},op0{},op1{},op2{};std::int32_t flagDisp{},clockDisp{};
    if(!read(helper,6,cmp) || cmp!=0x3D80 || !read(helper,0xC,zero) || zero!=0
       || !read(helper,8,flagDisp) || !read(helper,0x10,op0) || op0!=0x48
       || !read(helper,0x11,op1) || op1!=0x8B || !read(helper,0x12,op2) || op2!=0x05
       || !read(helper,0x13,clockDisp)) return false;
    const auto at=reinterpret_cast<std::uintptr_t>(helper);
    if(at!=image+0x3CC890 || at+0xD+flagDisp!=image+0x26B3920
       || at+0x17+clockDisp!=image+0x26B3928) return false;
    clockRoot.store(reinterpret_cast<const std::byte*>(image+0x26B3920),std::memory_order_release);
    return true;
}
inline bool replace_priority(std::uint32_t* output,std::uint32_t expected) noexcept {
    __try {
        if(!output || *output!=expected) return false;
        *output=entity_movement_policy::promote_movement(expected);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool pending(const void* descriptor) noexcept {
    std::uint32_t tag{};
    if(!read(descriptor,0x18,tag)) return false;
    const auto index=static_cast<std::int32_t>((tag&0x3FFFU)^0x2000U)-0x2000;
    if(index==0) {
        std::uint32_t mask{};
        return read(descriptor,0x10,mask) && mask!=0;
    }
    if(index==-1 || index==-2) {
        const void* bits{}; std::uint64_t mask{};
        return read(descriptor,0x10,bits) && read(bits,0,mask) && mask!=0;
    }
    return false;
}
inline bool registered_sobject(const std::byte* row) noexcept {
    // read the exact pool used by native 1704380 / 352400; never infer the class from an
    // EDZ-specific definition index (145), run55's identical schemas use 48
    const auto pool=sobjectPool.load(std::memory_order_acquire);
    std::uint32_t datum{},capacity{},stride{},markerOffset{},markerStride{},bank{},tag{},poolKind{};
    std::uint16_t definition{},headerSize{},updateSize{};
    const std::byte* allocator{};const std::byte* bitmap{};const std::byte* bits{};
    const std::byte* records{};const std::byte* header{};
    if(!pool || !read(row,2,definition) || definition>255
       || !read(row,4,datum) || datum==UINT32_MAX
       || !read(row,0x2C,headerSize) || headerSize!=16
       || !read(row,0x2E,updateSize) || !updateSize || updateSize>0x4000
       || !read(row,0x30,header) || !read(header,0,tag) || (tag&0x80000000U)==0
       || !read(pool,0x30,poolKind) || poolKind!=0x80806ACCU
       || !read(pool,0x10,stride) || stride!=12
       || !read(pool,0x14,capacity) || capacity!=1024
       || !read(pool,0x1C,markerOffset) || markerOffset!=0x3000
       || !read(pool,0x20,markerStride) || markerStride!=4
       || !read(pool,0x2C,bank) || bank>7
       || !read(pool,0,allocator) || !read(allocator,8,bitmap)
       || !read(bitmap,8,capacity) || !capacity || capacity>1024
       || !read(bitmap,16,bits) || !read(pool,8,records)) return false;
    const auto index=(datum&0x1FFFU)|((bank<<13)&0xFFFFU);
    std::uint32_t allocated{},proxy{};std::uint8_t salt{};
    if(index>=capacity || !read(bits,(index>>5)*4,allocated)
       || (allocated&(1U<<(index&31)))==0
       || !read(records,markerOffset+static_cast<std::size_t>(index)*markerStride,salt)
       || salt!=static_cast<std::uint8_t>(datum>>23)
       || !read(records,static_cast<std::size_t>(datum&0x1FFFU)*stride+4,proxy)
       || !proxy || proxy>=0x80000000U) return false;
    std::uint32_t current{};std::uint16_t currentDefinition{};
    return read(row,4,current) && current==datum
        && read(row,2,currentDefinition) && currentDefinition==definition;
}
inline bool identify(const std::byte* rows,void* lane,std::uint32_t packed,Key& key,
                     std::uint64_t& stamp,std::uint32_t channel=1) noexcept {
    // the packed index is 15 bits; native entity tokens only use the low 13
    const auto index=packed&0x7FFFU;
    // 17B0D70 supplies the scheduler channel, which 170C080 encodes in bits 29..30. Tower's active
    // sobjects use channel 0, patrols channel 1; this is not lane+C's per-peer descriptor index
    if(channel>1 || index>0x1FFF || ((packed>>29)&3U)!=channel || ((packed>>27)&3U)!=2) return false;
    const void* manager{}; std::int32_t view{}; std::uint8_t enabledA{},enabledB{};
    if(!read(lane,9,enabledA) || !read(lane,10,enabledB) || !enabledA || !enabledB
       || !read(lane,0x10,manager) || !manager || !read(lane,0xC,view) || view<0 || view>=32) return false;
    std::int16_t rowIndex{-1};
    if(!rows || !read(manager,0x114+index*6,rowIndex) || rowIndex<0 || rowIndex>=1024) return false;
    const auto row=rows+static_cast<std::size_t>(rowIndex)*0x70;
    std::uint8_t type{},owner{}; std::uint16_t globalFlags{};
    std::uint32_t token{},viewMask{}; const std::byte* aux{};
    if(!read(row,0,type) || type!=0 || !read(row,1,owner) || owner!=0xFF
       || !registered_sobject(row) || !read(row,8,token)
       || token==0xFFFFFFFF || (token&0x1FFFU)!=index
       || !read(row,0x50,globalFlags) || (globalFlags&0x200U)!=0
       || !read(row,0x60,aux) || !aux || !read(aux,4,viewMask)
       || (viewMask&(1U<<view))==0) return false;
    const auto descriptor=aux+0x18+static_cast<std::size_t>(view)*0x40;
    std::uint16_t flags{};
    if(!read(descriptor,0x38,flags) || (flags&0x83U)!=0x82U || !pending(descriptor)
       || !read(descriptor,0x20,stamp)) return false;
    key={lane,manager,descriptor,token,static_cast<std::uint32_t>(view),channel};
    return true;
}
inline void apply(void* lane,std::uint32_t view,std::uint32_t laneIndex,
                  std::int32_t capacity,std::int32_t count,std::uint32_t* output,
                  std::uint64_t now,Clock clock={}) noexcept {
    const auto rows=table.load(std::memory_order_acquire);
    if(!rows || !lane || view>1 || laneIndex!=2 || count<0 || count>capacity
       || capacity<0 || capacity>2000 || (count>0 && !output)) return;
    if(!TryAcquireSRWLockExclusive(&lock)) return;
    ++observation.gathers;
    observation.candidates += static_cast<std::uint64_t>(count);
    policy.begin(lane,view);
    for(std::int32_t i=0;i<count;++i) {
        std::uint32_t packed{}; Key key{}; std::uint64_t stamp{};
        if(!read(output,static_cast<std::size_t>(i)*4,packed) || !identify(rows,lane,packed,key,stamp,view)) continue;
        ++observation.eligible;
        const auto decision=policy.observe(key,stamp,now,clock);
        observation.stampAdvances += decision.stampAdvanced ? 1U : 0U;
        observation.advancesAfterBoost += decision.advancedAfterBoost ? 1U : 0U;
        observation.overdue += decision.overdue ? 1U : 0U;
        if(decision.nativeAgeKnown) {
            ++observation.nativeAgeSamples;
            if(decision.ageMs>=observation.maxNativeAgeMs) {
                observation.maxNativeAgeMs=decision.ageMs;
                observation.oldestToken=key.token;
                observation.oldestPeer=key.view;
                observation.oldestStamp=stamp;
                observation.oldestPriority=packed&kPriorityMask;
                observation.channel=view;
            }
        }
        if(decision.overdue && (packed&kPriorityMask)<entity_movement_policy::kMovementPriority
           && replace_priority(output+i,packed)) {
            policy.mark_boost(key);
            ++observation.boosted;
        }
    }
    policy.finish(lane,view);
    Observation snapshot{};
    const bool reportDue=lastReport==0 || now<lastReport || now-lastReport>=5000;
    if(reportDue) { snapshot=observation; observation={}; lastReport=now; }
    ReleaseSRWLockExclusive(&lock);
    if(reportDue) report(snapshot);
}
inline bool configure(const std::byte* gather) noexcept {
    table.store(nullptr,std::memory_order_release);
    clockRoot.store(nullptr,std::memory_order_release);
    sobjectPool.store(nullptr,std::memory_order_release);
    std::uint8_t a{},b{},c{}; std::int32_t displacement{};
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!image || reinterpret_cast<std::uintptr_t>(gather)!=image+0x170C080
       || !read(gather,0x71,a) || !read(gather,0x72,b) || !read(gather,0x73,c)
       || a!=0x4C || b!=0x8D || c!=0x15 || !read(gather,0x74,displacement)) return false;
    const auto resolved=reinterpret_cast<std::uintptr_t>(gather)+0x78+displacement;
    if(resolved!=image+0x30B0440) return false;
    const auto getter=reinterpret_cast<const std::byte*>(image+0x1704380);
    constexpr std::array<std::uint8_t,13> entry{0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9,0x83,0xF9,0xFF,0x74,0x62};
    for(std::size_t i=0;i<entry.size();++i) {std::uint8_t value{};if(!read(getter,i,value) || value!=entry[i]) return false;}
    std::int32_t poolDisp{},callDisp{};std::uint8_t lea0{},lea1{},lea2{},call{};
    if(!read(getter,0x22,lea0) || lea0!=0x48 || !read(getter,0x23,lea1) || lea1!=0x8D
       || !read(getter,0x24,lea2) || lea2!=0x0D || !read(getter,0x25,poolDisp)
       || image+0x1704380+0x29+poolDisp!=image+0x2037D40
       || !read(getter,0x38,call) || call!=0xE8 || !read(getter,0x39,callDisp)
       || image+0x1704380+0x3D+callDisp!=image+0x352400) return false;
    sobjectPool.store(reinterpret_cast<const std::byte*>(image+0x2037D40),std::memory_order_release);
    table.store(reinterpret_cast<const std::byte*>(resolved),std::memory_order_release);
    configure_clock(reinterpret_cast<const std::byte*>(image+0x3CC890),image);
    return true;
}
} // namespace dawn::client::hooks::bootflow::entity_movement_fairness
