#pragma once

#include "../../hooking/detour.h"
#include "remote_weapon_native_wake.h"

#ifndef RESTORATION_NATIVE_WEAPON_PENDING_OMISSION
#define RESTORATION_NATIVE_WEAPON_PENDING_OMISSION 0
#endif
#ifndef RESTORATION_NATIVE_WEAPON_APPLY_OMISSION
#define RESTORATION_NATIVE_WEAPON_APPLY_OMISSION 0
#endif

#ifndef RESTORATION_REMOTE_WEAPON_SHOT_TRACE
#define RESTORATION_REMOTE_WEAPON_SHOT_TRACE 0
#endif

namespace dawn::client::hooks::bootflow::weapon_frame_wake {
namespace memory = entity_movement_fairness;
using Pending = std::uint8_t(__fastcall*)(void*, const void*) noexcept;
using Apply = void(__fastcall*)(void*, const void*) noexcept;
using Shot = void(__fastcall*)(void*, const void*, const void*, float) noexcept;
inline std::array<hooking::detour::Handle, 3> handles{};
inline std::atomic<Pending> originalPending{};
inline std::atomic<Apply> originalApply{};
inline std::atomic<Shot> originalShot{};
inline std::atomic<unsigned> activeCalls{};
inline std::atomic_bool enabled{};
inline thread_local unsigned nesting{};
inline SRWLOCK statsLock = SRWLOCK_INIT;
inline std::array<std::uint64_t, 3> calls{}, wakes{};
inline std::uint64_t lastReport{};
struct Lease {
    Lease() noexcept { activeCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~Lease() { activeCalls.fetch_sub(1, std::memory_order_acq_rel); }
};
inline void report(std::size_t source, bool woke) noexcept {
    if (!TryAcquireSRWLockExclusive(&statsLock)) return;
    ++calls[source]; if (woke) ++wakes[source];
    const auto now = GetTickCount64();
    std::array<char, 384> line{}; int length{};
    if (now < lastReport || now - lastReport >= 5000) {
        length = std::snprintf(line.data(), line.size(),
            "ev=weapon_frame_wake stage=runtime pending_calls=%llu pending_wakes=%llu"
            " apply_calls=%llu apply_wakes=%llu shot_calls=%llu shot_wakes=%llu mutation=native_frame_queue_only",
            static_cast<unsigned long long>(calls[0]), static_cast<unsigned long long>(wakes[0]),
            static_cast<unsigned long long>(calls[1]), static_cast<unsigned long long>(wakes[1]),
            static_cast<unsigned long long>(calls[2]), static_cast<unsigned long long>(wakes[2]));
        calls = {}; wakes = {}; lastReport = now;
    }
    ReleaseSRWLockExclusive(&statsLock);
    if (length > 0 && static_cast<std::size_t>(length) < line.size())
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(length)});
}
#if RESTORATION_REMOTE_WEAPON_SHOT_TRACE
struct ShotSnapshot {
    std::uint8_t phase{}, frameSelector{};
    float timer{};
};
inline bool shot_snapshot(const void* component, std::uintptr_t image,
                                const remote_weapon_native_wake::Request& identity,
                                ShotSnapshot& output) noexcept {
    std::uintptr_t current{};
    std::uint32_t asset{}, type{}, owner{}, rootAsset{}, rootOwner{}, legacy{};
    std::uint64_t parent{};
    const auto expected = reinterpret_cast<std::uintptr_t>(identity.root);
    return remote_weapon_native_wake::resolve(image, identity.owner, current)
        && current == expected
        && current + 0xD70 == reinterpret_cast<std::uintptr_t>(component)
        && memory::read(component, 0, asset) && asset == identity.asset
        && memory::read(component, 4, type) && type == 0x80803865U
        && memory::read(component, 0x98, owner) && owner == identity.owner
        && memory::read(component, 0xA0, parent) && parent == 0
        && memory::read(identity.root, 0, rootAsset) && rootAsset == asset
        && memory::read(identity.root, 0x24, rootOwner) && rootOwner == owner
        && memory::read(identity.root, 0x2C, legacy) && legacy == identity.legacy
        && memory::read(component, 0x16F9, output.phase)
        && memory::read(component, 0x16FC, output.timer)
        && memory::read(identity.root, 0x7B0, output.frameSelector)
        && remote_weapon_native_wake::resolve(image, identity.owner, current)
        && current == expected;
}
#endif
#if RESTORATION_REMOTE_WEAPON_SHOT_TRACE
inline std::atomic<unsigned> shotDiagnosticBudget{32};
#endif
inline bool shot_request(void* component, bool afterShot) noexcept {
#if RESTORATION_REMOTE_WEAPON_SHOT_TRACE
    if (!shotDiagnosticBudget.load(std::memory_order_relaxed))
        return remote_weapon_native_wake::request(component, afterShot);
    const auto savedError=GetLastError();
    const auto image=remote_weapon_native_wake::imageBase.load(std::memory_order_acquire);
    remote_weapon_native_wake::Request identity{}; ShotSnapshot before{},after{};
    bool observe=remote_weapon_native_wake::prepare(component,image,identity,afterShot)
        && shot_snapshot(component,image,identity,before);
    if (observe) {
        auto left=shotDiagnosticBudget.load(std::memory_order_relaxed);
        while(left && !shotDiagnosticBudget.compare_exchange_weak(left,left-1,std::memory_order_relaxed)) {}
        observe=left!=0;
    }
    const bool requested=remote_weapon_native_wake::request(component,afterShot);
    if(observe) {
        const bool qualified=shot_snapshot(component,image,identity,after);
        std::array<char,768> line{};
        const auto size=std::snprintf(line.data(),line.size(),
            "ev=weapon_frame_wake stage=shot_request_diagnostic owner=%08X legacy=%08X asset=%08X"
            " requested=%u after_qualified=%u record_null=%u phase=%u/%u timer=%.9g/%.9g"
            " frame_selector=%02X/%02X queue_admission_change=unobserved measurement=best_effort_state_only",
            identity.owner,identity.legacy,identity.asset,static_cast<unsigned>(requested),
            static_cast<unsigned>(qualified),static_cast<unsigned>(afterShot),
            static_cast<unsigned>(before.phase),static_cast<unsigned>(after.phase),
            static_cast<double>(before.timer),static_cast<double>(after.timer),
            static_cast<unsigned>(before.frameSelector),static_cast<unsigned>(after.frameSelector));
        if(size>0 && static_cast<std::size_t>(size)<line.size())
            core::log::write(core::log::Channel::client,core::log::Level::info,{line.data(),static_cast<std::size_t>(size)});
    }
    SetLastError(savedError);return requested;
#else
    return remote_weapon_native_wake::request(component,afterShot);
#endif
}

__declspec(noinline) inline std::uint8_t __fastcall pending(void* component, const void* context) noexcept {
    const Lease lease{};
    const auto native = originalPending.load(std::memory_order_acquire);
    if (!native) return 0;
    if ((nesting & 1U) || !enabled.load(std::memory_order_acquire)) return native(component, context);
    nesting |= 1U;
    const auto result = native(component, context); const auto error = GetLastError();
    report(0, remote_weapon_native_wake::request(component));
    nesting &= ~1U; SetLastError(error); return result;
}
__declspec(noinline) inline void __fastcall apply(void* component, const void* source) noexcept {
    const Lease lease{};
    const auto native = originalApply.load(std::memory_order_acquire);
    if (!native) return;
    if ((nesting & 2U) || !enabled.load(std::memory_order_acquire)) { native(component, source); return; }
    nesting |= 2U;
    native(component, source); const auto error = GetLastError();
    report(1, remote_weapon_native_wake::request(component));
    nesting &= ~2U; SetLastError(error);
}
__declspec(noinline) inline void __fastcall shot(void* component, const void* receivedRecord,
                                               const void* pendingData, float delay) noexcept {
    const Lease lease{};
    const auto native = originalShot.load(std::memory_order_acquire);
    if (!native) return;
    if ((nesting & 4U) || !enabled.load(std::memory_order_acquire)) {
        native(component, receivedRecord, pendingData, delay); return;
    }
    nesting |= 4U;
    native(component, receivedRecord, pendingData, delay); const auto error = GetLastError();
    report(2, shot_request(component, receivedRecord == nullptr));
    nesting &= ~4U; SetLastError(error);
}
template<std::size_t N> inline bool matches(const void* image, std::size_t offset,
                                           const std::array<std::uint8_t, N>& expected) noexcept {
    std::array<std::uint8_t, N> actual{};
    return memory::read(image, offset, actual) && actual == expected;
}
inline void install() noexcept {
#if 0

    if (handles[2].attached) return;
    auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    std::uintptr_t pendingSlot{}, applySlot{};
    const bool supported = image
        && matches(image, 0xCC4010, std::array<std::uint8_t,14>{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x4C,0x8B,0x42,0x08})
        && matches(image, 0xCCF6A0, std::array<std::uint8_t,15>{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20})
        && matches(image, 0xCB48C0, std::array<std::uint8_t,5>{0xE9,0xA5,0x06,0x2B,0x04})
        && matches(image, 0xCB48FB, std::array<std::uint8_t,11>{0x49,0x8B,0xF8,0x48,0x8B,0xD9,0x8B,0x09,0x0F,0x28,0xF3})
        && memory::read(image, 0x1D03650, pendingSlot) && pendingSlot == reinterpret_cast<std::uintptr_t>(image + 0xCCF6A0)
        && memory::read(image, 0x1D03630, applySlot) && applySlot == reinterpret_cast<std::uintptr_t>(image + 0xCC4010);
    if (supported) {
        remote_weapon_native_wake::configure(reinterpret_cast<std::uintptr_t>(image));
        const std::array<hooking::detour::Spec, 3> specs{{
            {image + 0xCCF6A0, reinterpret_cast<void*>(&pending)},
            {image + 0xCC4010, reinterpret_cast<void*>(&apply)},
            {image + 0xCB48C0, reinterpret_cast<void*>(&shot)}}};
        bool attached{};
        if (remote_weapon_native_wake::nativeWake.load(std::memory_order_acquire)) {
#if RESTORATION_NATIVE_WEAPON_PENDING_OMISSION && RESTORATION_NATIVE_WEAPON_APPLY_OMISSION
            attached = hooking::detour::install(specs[2], handles[2]);
#elif RESTORATION_NATIVE_WEAPON_PENDING_OMISSION
            attached = hooking::detour::install(std::span<const hooking::detour::Spec>(specs).subspan(1),
                                               std::span<hooking::detour::Handle>(handles).subspan(1));
#elif RESTORATION_NATIVE_WEAPON_APPLY_OMISSION
            const std::array<hooking::detour::Spec, 2> selected{{specs[0], specs[2]}};
            std::array<hooking::detour::Handle, 2> acquired{};
            attached = hooking::detour::install(selected, acquired);
            if (attached) { handles[0] = acquired[0]; handles[2] = acquired[1]; }
#else
            attached = hooking::detour::install(specs, handles);
#endif
        }
        if (attached) {
            originalPending.store(reinterpret_cast<Pending>(handles[0].original), std::memory_order_release);
            originalApply.store(reinterpret_cast<Apply>(handles[1].original), std::memory_order_release);
            originalShot.store(reinterpret_cast<Shot>(handles[2].original), std::memory_order_release);
            enabled.store(true, std::memory_order_release);
        } else remote_weapon_native_wake::disable();
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
        handles[2].attached
#if RESTORATION_NATIVE_WEAPON_PENDING_OMISSION && RESTORATION_NATIVE_WEAPON_APPLY_OMISSION
        ? "ev=weapon_frame_wake stage=install result=ok policy=post_shot_only_trial hooks=1 mutation=native_frame_queue_only"
#elif RESTORATION_NATIVE_WEAPON_PENDING_OMISSION
        ? "ev=weapon_frame_wake stage=install result=ok policy=pending_native_trial hooks=2 mutation=native_frame_queue_only"
#elif RESTORATION_NATIVE_WEAPON_APPLY_OMISSION
        ? "ev=weapon_frame_wake stage=install result=ok policy=apply_native_trial hooks=2 mutation=native_frame_queue_only"
#else
        ? "ev=weapon_frame_wake stage=install result=ok policy=weapon_v5_remote_only hooks=3 mutation=native_frame_queue_only"
#endif
        : "ev=weapon_frame_wake stage=install result=disabled reason=unsupported_layout_or_hook");

#endif
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=weapon_frame_wake stage=install result=native hooks=0 compatibility=retired");
}
inline bool uninstall() noexcept {
    enabled.store(false, std::memory_order_release);
    if (handles[2].attached) {
        const std::array<hooking::detour::ProtectedCodeEntry, 3> entries{{
            {reinterpret_cast<void*>(&pending)}, {reinterpret_cast<void*>(&apply)}, {reinterpret_cast<void*>(&shot)}}};
        if (hooking::detour::uninstall(handles, entries,
            +[]() noexcept { return activeCalls.load(std::memory_order_acquire) == 0; })
            != hooking::detour::UninstallResult::removed) {
            enabled.store(true, std::memory_order_release);
            return false;
        }
    }
    remote_weapon_native_wake::disable();
    originalPending = nullptr; originalApply = nullptr; originalShot = nullptr;
    AcquireSRWLockExclusive(&statsLock);
    calls = {}; wakes = {}; lastReport = 0;
    ReleaseSRWLockExclusive(&statsLock);
    return true;
}
}
