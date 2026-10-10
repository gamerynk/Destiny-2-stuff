#pragma once

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "entity_movement_fairness.h"
#include "remote_facing_policy.h"
#include "remote_facing_diagnostic.h"

// on-foot subset of the RUN100 facing repair for PC 1.59: received direction advanced while
// control/facing fields stayed frozen. Stopped-position/velocity drift is a separate open issue
namespace dawn::client::hooks::bootflow::remote_facing_control {
namespace memory = entity_movement_fairness;
namespace policy = remote_facing_policy;
using Apply = void(__fastcall*)(void*, const void*, void*) noexcept;
using ControlPass = void(__fastcall*)() noexcept;
using NativeControl = void(__fastcall*)(void*, void*) noexcept;
inline std::array<hooking::detour::Handle,2> handles{};
inline std::atomic<Apply> originalApply{};
inline std::atomic<ControlPass> originalPass{};
inline std::atomic<NativeControl> nativeDirection{};
inline std::atomic<std::uintptr_t> imageBase{};
inline std::atomic<std::uint32_t> activeCalls{};
inline SRWLOCK queueLock = SRWLOCK_INIT;
inline SRWLOCK controlLock = SRWLOCK_INIT;
inline policy::Queue pending{};
inline thread_local bool inApply{}, inPass{};
inline std::atomic<std::uint64_t> publications{};

struct ActiveCall final {
    ActiveCall() noexcept { activeCalls.fetch_add(1,std::memory_order_acq_rel); }
    ~ActiveCall() { activeCalls.fetch_sub(1,std::memory_order_acq_rel); }
};
inline bool idle() noexcept { return activeCalls.load(std::memory_order_acquire) == 0; }

inline bool identify(const void* component, policy::Key& key, policy::State& state) noexcept {
    key={}; state={};
    const auto image=imageBase.load(std::memory_order_acquire);
    key.component=reinterpret_cast<std::uintptr_t>(component);
    std::uint32_t type{}; std::int64_t parent{};
    if (!image || key.component<0x10000 || key.component>=0x7FFFFFFFFFFFULL || key.component%16
        || !memory::read(component,0,key.asset) || key.asset!=0x80C0C51AU
        || !memory::read(component,4,type) || type!=0x80803A00U
        || !memory::read(component,0x10,parent) || parent!=0
        || !memory::read(component,0x24,key.owner) || !key.owner || key.owner>=0x80000000U
        || !memory::read(component,0x2C,key.legacy) || key.legacy==UINT32_MAX) return false;
    const void* root{}; const std::byte* table{}; const std::byte* rows{};
    const auto shifted=key.owner>>13;
    const auto bank=(((shifted|0x0FFC0000U)>>18)&(shifted&0xFFFFU));
    std::uint32_t stride{}; std::int32_t mask{}; std::uint64_t relative{};
    if (!memory::read(reinterpret_cast<const void*>(image+0x2439C70),0,root)
        || !memory::read(root,0,table) || !table) return false;
    const auto* descriptor=table+static_cast<std::size_t>(bank)*0x40;
    if (!memory::read(descriptor,8,rows) || !rows
        || !memory::read(descriptor,0x30,stride) || stride<16 || stride>0x100000
        || !memory::read(descriptor,0x34,mask)) return false;
    const auto raw=reinterpret_cast<std::uintptr_t>(rows)+static_cast<std::size_t>(key.owner&0x1FFFU)*stride;
    if (!memory::read(reinterpret_cast<const void*>(raw),8,relative)
        || raw-(relative&static_cast<std::uint64_t>(static_cast<std::int64_t>(mask)))!=key.component) return false;
    const auto index=key.legacy&0x1FFFU;
    std::uint32_t authority{}, suppressed{}; const std::byte* suppression{};
    std::array<float,3> facing{}, secondaryFacing{};
    if (!memory::read(reinterpret_cast<const void*>(image+0x26BE0E0),(index>>5)*4,authority)
        || !memory::read(reinterpret_cast<const void*>(image+0x1FBA9C8),0,suppression)
        || !memory::read(suppression,(index>>5)*4,suppressed)
        || !memory::read(component,0xACE,state.classification)
        || !memory::read(component,0x410,state.controlEpoch)
        || !memory::read(component,0x8A0,state.vehicle)
        || !memory::read(component,0xB0,state.flags)
        || !memory::read(component,0xBF8,state.transition)
        || !memory::read(component,0x420,state.received)
        || !memory::read(component,0xD0,state.control)
        || !memory::read(component,0xE0,state.secondary)
        || !memory::read(component,0xB14,facing)
        || !memory::read(component,0xB38,secondaryFacing)) return false;
    state.facing={facing[0],facing[1],facing[2],0};
    state.secondaryFacing={secondaryFacing[0],secondaryFacing[1],secondaryFacing[2],0};
    state.identity=true;
    state.authority=(authority&(1U<<(index&31)))!=0;
    state.suppressed=(suppressed&(1U<<(index&31)))!=0;
    key.controlEpoch=state.controlEpoch;
    return true;
}

inline bool publish_facing(const policy::Key& expected, const policy::Vector& value) noexcept {
    auto* component=reinterpret_cast<std::byte*>(expected.component);
    policy::Key current{}; policy::State state{};
    if (!identify(component,current,state) || current!=expected || !policy::supported(state)
        || !policy::valid(value) || policy::different(state.received,value)
        || policy::different(state.received,state.control)
        || policy::different(state.received,state.secondary)) return false;
    __try {
        if (*reinterpret_cast<const std::uint32_t*>(component)!=expected.asset
            || *reinterpret_cast<const std::uint32_t*>(component+0x24)!=expected.owner
            || *reinterpret_cast<const std::uint32_t*>(component+0x2C)!=expected.legacy
            || *reinterpret_cast<const std::uint8_t*>(component+0x410)!=expected.controlEpoch
            || *reinterpret_cast<const std::uint32_t*>(component+0x8A0)!=UINT32_MAX
            || *reinterpret_cast<const std::uint32_t*>(component+0xB0)!=0
            || *reinterpret_cast<const std::uint8_t*>(component+0xBF8)!=0
            || std::memcmp(component+0x420,&state.received,sizeof(state.received))!=0
            || std::memcmp(component+0xD0,&state.control,sizeof(state.control))!=0
            || std::memcmp(component+0xE0,&state.secondary,sizeof(state.secondary))!=0) return false;
        const std::array<float,3> xyz{value.x,value.y,value.z};
        *reinterpret_cast<std::array<float,3>*>(component+0xB38)=xyz;
        *reinterpret_cast<std::array<float,3>*>(component+0xB14)=xyz;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

__declspec(noinline) inline void __fastcall apply(void* component, const void* source, void* context) noexcept {
    ActiveCall active;
    const auto native=originalApply.load(std::memory_order_acquire); if (!native) return;
    if (inApply) { native(component,source,context); return; }
    inApply=true;
    remote_facing_diagnostic::observe(imageBase.load(std::memory_order_acquire),component,"apply_pre_original",&identify,true);
    native(component,source,context); const auto error=GetLastError();
    remote_facing_diagnostic::observe(imageBase.load(std::memory_order_acquire),component,"apply_post_original_pre_queue",&identify,true);
    policy::Key key{}; policy::State state{};
    if (identify(component,key,state) && policy::eligible(state)) {
        AcquireSRWLockExclusive(&queueLock);
        (void)pending.push(key,GetTickCount64());
        ReleaseSRWLockExclusive(&queueLock);
    }
    inApply=false; SetLastError(error);
}
__declspec(noinline) inline void __fastcall control_pass() noexcept {
    ActiveCall active;
    const auto native=originalPass.load(std::memory_order_acquire); if (!native) return;
    if (inPass) { native(); return; }
    inPass=true;
    remote_facing_diagnostic::observe(imageBase.load(std::memory_order_acquire),nullptr,"control_pre_original",&identify);
    native(); const auto error=GetLastError();
    remote_facing_diagnostic::observe(imageBase.load(std::memory_order_acquire),nullptr,"control_post_original_pre_repair",&identify);
    if (!TryAcquireSRWLockExclusive(&controlLock)) { inPass=false; SetLastError(error); return; }
    policy::Queue work{};
    AcquireSRWLockExclusive(&queueLock); work=pending; pending={}; ReleaseSRWLockExclusive(&queueLock);
    const auto now=GetTickCount64();
    const auto direction=nativeDirection.load(std::memory_order_acquire);
    if (imageBase.load(std::memory_order_acquire) && direction) for (const auto& entry:work.entries) {
        if (!entry.key.component) continue;
        policy::Key current{}; policy::State state{};
        auto* component=reinterpret_cast<std::byte*>(entry.key.component);
        if (!identify(component,current,state) || !policy::applies(entry,current,state,now)) continue;
        // this helper only syncs the received direction into control; do not replay 1825A50, which
        // consumes unrelated controls and player state
        direction(component,component+0xB0);
        if (publish_facing(current,state.received)) {
            const auto count=publications.fetch_add(1,std::memory_order_relaxed)+1;
            if ((count&(count-1))==0) {
                std::array<char,160> line{};
                const auto size=std::snprintf(line.data(),line.size(),
                    "ev=remote_facing_control stage=publish result=ok count=%llu scope=remote_onfoot",
                    static_cast<unsigned long long>(count));
                if (size>0 && static_cast<std::size_t>(size)<line.size())
                    core::log::write(core::log::Channel::client,core::log::Level::info,
                        {line.data(),static_cast<std::size_t>(size)});
            }
        }
    }
    ReleaseSRWLockExclusive(&controlLock);
    remote_facing_diagnostic::observe(imageBase.load(std::memory_order_acquire),nullptr,"control_post_repair",&identify);
    inPass=false; SetLastError(error);
}

template<std::size_t N> inline bool matches(std::uintptr_t at, const std::array<std::uint8_t,N>& bytes) noexcept {
    std::array<std::uint8_t,N> actual{};
    return memory::read(reinterpret_cast<const void*>(at),0,actual) && actual==bytes;
}
inline void install() noexcept {
#if 0

    if (handles[0].attached || handles[1].attached) return;
    const auto image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const std::array<std::uint8_t,16> applyEntry{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xC0};
    const std::array<std::uint8_t,21> passEntry{0x40,0x56,0x48,0x83,0xEC,0x30,0x48,0x89,0x6C,0x24,0x48,0xE8,0x20,0xA5,0xFF,0xFF,0x33,0xED,0x48,0x8B,0xF0};
    const std::array<std::uint8_t,10> readEntry{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20};
    const std::array<std::uint8_t,69> facingPublish{
        0x8B,0x07,0xC1,0xE8,0x12,0xA8,0x01,0x75,0x3C,
        0x48,0x8D,0x4F,0x30,0xE8,0x6A,0x35,0xAF,0xFE,0x48,0x8D,0x4F,0x20,
        0xF2,0x0F,0x10,0x00,0xF2,0x0F,0x11,0x86,0x38,0x0B,0x00,0x00,
        0x8B,0x40,0x08,0x89,0x86,0x40,0x0B,0x00,0x00,0xE8,0x4C,0x35,0xAF,0xFE,
        0xF2,0x0F,0x10,0x00,0xF2,0x0F,0x11,0x86,0x14,0x0B,0x00,0x00,
        0x8B,0x40,0x08,0x89,0x86,0x1C,0x0B,0x00,0x00};
    const std::array<std::uint8_t,4> vectorIdentity{0x48,0x8B,0xC1,0xC3};
    const std::array<std::uint8_t,5> readCall{0xE8,0xAB,0xA5,0x00,0x00};
    const std::array<std::uint8_t,31> epochCycle{
        0x0F,0xB6,0x8B,0x58,0x08,0x00,0x00,0xBA,0x02,0x00,0x00,0x00,
        0xFE,0xC1,0x80,0xE1,0x0F,0x3A,0xCA,0x0F,0xB6,0xC1,0x0F,0x42,0xC2,
        0x88,0x83,0x58,0x08,0x00,0x00};
    if (image && matches(image+0xBC5670,applyEntry) && matches(image+0xBC9300,passEntry)
        && matches(image+0xBC71D0,readEntry) && matches(image+0x1825C74,facingPublish)
        && matches(image+0x3191F0,vectorIdentity) && matches(image+0xBBCC20,readCall)
        && matches(image+0x12EE84B,epochCycle)) {
        const std::array<hooking::detour::Spec,2> specs{{
            {reinterpret_cast<void*>(image+0xBC5670),reinterpret_cast<void*>(&apply)},
            {reinterpret_cast<void*>(image+0xBC9300),reinterpret_cast<void*>(&control_pass)}}};
        if (hooking::detour::install(specs,handles)) {
            originalApply.store(reinterpret_cast<Apply>(handles[0].original),std::memory_order_release);
            originalPass.store(reinterpret_cast<ControlPass>(handles[1].original),std::memory_order_release);
            nativeDirection.store(reinterpret_cast<NativeControl>(image+0xBC71D0),std::memory_order_release);
            imageBase.store(image,std::memory_order_release);
            remote_facing_diagnostic::start();
        }
    }
    core::log::write(core::log::Channel::client,core::log::Level::info,imageBase.load()
        ? "ev=remote_facing_control stage=install result=ok status=candidate scope=remote_onfoot publish=facing_xyz native_direction=1"
        : "ev=remote_facing_control stage=install result=disabled reason=unsupported_target_or_hook");

#endif
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=remote_facing_control stage=install result=native hooks=0 compatibility=retired");
}
[[nodiscard]] inline bool uninstall() noexcept {
    remote_facing_diagnostic::stop();
    imageBase.store(0,std::memory_order_release);
    if (!handles[0].attached && !handles[1].attached) return true;
    const std::array<hooking::detour::ProtectedCodeEntry,2> entries{{
        {reinterpret_cast<void*>(&apply)}, {reinterpret_cast<void*>(&control_pass)}}};
    const auto result=hooking::detour::uninstall(handles,entries,&idle);
    if (result!=hooking::detour::UninstallResult::removed) {
        core::log::write(core::log::Channel::client,core::log::Level::warn,
            "ev=remote_facing_control stage=uninstall result=retained mutation=disabled");
        return false;
    }
    originalApply.store(nullptr,std::memory_order_release);
    originalPass.store(nullptr,std::memory_order_release);
    nativeDirection.store(nullptr,std::memory_order_release);
    AcquireSRWLockExclusive(&queueLock); pending={}; ReleaseSRWLockExclusive(&queueLock);
    publications.store(0,std::memory_order_release);
    return true;
}

} // namespace dawn::client::hooks::bootflow::remote_facing_control
