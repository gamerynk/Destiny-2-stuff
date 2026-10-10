#pragma once

#include "../../hooking/detour.h"
#include "internal.h"
#include "entity_movement_fairness.h"
#include "combat_feedback_priority.h"
#include "entity_lifecycle_fairness.h"
#include "entity_creation_fairness.h"
#include "entity_handoff_fairness.h"

#ifndef RESTORATION_NATIVE_COMBAT_PRIORITY
#define RESTORATION_NATIVE_COMBAT_PRIORITY 0
#endif

namespace dawn::client::hooks::bootflow::entity_schedule_diagnostic {

using Gather = std::int32_t(__fastcall*)(void*, std::uint32_t, std::uint32_t, const void*,
                                        std::int32_t, std::uint32_t*) noexcept;
inline hooking::detour::Handle gatherHandle{};
inline hooking::detour::Handle eventGatherHandle{};
inline std::atomic<Gather> originalGather{};
inline std::atomic<Gather> originalEventGather{};
inline std::atomic<unsigned> activeCalls{};
inline std::atomic_bool enabled{};
inline std::atomic<std::uint64_t> removalsPromoted{}, lastRemovalReport{};
inline std::atomic<std::uint64_t> creationsPromoted{};
inline std::atomic<std::uint64_t> handoffsPromoted{};

struct CallLease {
    CallLease() noexcept { activeCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~CallLease() { activeCalls.fetch_sub(1, std::memory_order_acq_rel); }
};

inline bool idle() noexcept {
    return activeCalls.load(std::memory_order_acquire) == 0;
}

__declspec(noinline) inline std::int32_t __fastcall gather_events(
    void* lane, std::uint32_t channel, std::uint32_t laneIndex, const void* context,
    std::int32_t capacity, std::uint32_t* output) noexcept {
    const CallLease lease{};
    const auto native = originalEventGather.load(std::memory_order_acquire);
    if (!native) return 0;
    const auto result = native(lane, channel, laneIndex, context, capacity, output);
    const auto error = GetLastError();
    if (enabled.load(std::memory_order_acquire))
        combat_feedback_priority::apply(lane, channel, laneIndex, capacity, result, output);
    SetLastError(error);
    return result;
}

inline void install_feedback() noexcept {
#if 0

    if (eventGatherHandle.attached) return;
    constexpr std::string_view pattern =
        "40 55 56 57 41 54 41 55 48 81 EC C0 0A 00 00 48 8B 05 ? ? ? ? 48 33 C4";
    const auto target = scan_main_image_unique(signature<signature_length(pattern)>(pattern),
                                               "combat_feedback.gather");
    const auto image = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    bool supported = target && reinterpret_cast<std::uintptr_t>(target) == image + 0x170C3F0;
    constexpr std::array<std::uintptr_t, 12> slots{
        0x1C16E10, 0x1C16CE0, 0x1C16D70, 0x1C31A18, 0x1C16E98, 0x1C16F20,
        0x1C16FB0, 0x1CA19D0, 0x1C9FBA8, 0x1CA1AF8, 0x1CA1708, 0x1C9FFA0};
    constexpr std::array<std::uintptr_t, 12> handlers{
        0xB7D690, 0xB7D710, 0xB7D610, 0xEF60A0, 0xB7D790, 0xB7D810,
        0xB7DAD0, 0x1723750, 0x16E6AA0, 0x17238C0, 0x1723570, 0x16E6C80};
    for (std::size_t i = 0; supported && i < slots.size(); ++i) {
        std::uintptr_t method{};
        supported = entity_movement_fairness::read(reinterpret_cast<void*>(image), slots[i], method)
            && method == image + handlers[i];
    }
    constexpr std::array<std::uint8_t, 9> lookup{0x48,0x63,0xC2,0x48,0x8B,0x44,0xC1,0x30,0xC3};
    for (std::size_t i = 0; supported && i < lookup.size(); ++i) {
        std::uint8_t byte{};
        supported = entity_movement_fairness::read(reinterpret_cast<void*>(image), 0x16EAEA0+i, byte)
            && byte == lookup[i];
    }
    if (supported && hooking::detour::install({target, reinterpret_cast<void*>(&gather_events)}, eventGatherHandle)) {
        originalEventGather.store(reinterpret_cast<Gather>(eventGatherHandle.original), std::memory_order_release);
        combat_feedback_priority::imageBase.store(image, std::memory_order_release);
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
        eventGatherHandle.attached
            ? "ev=combat_feedback stage=install result=ok policy=combat_v5 feedback_priority=1023 movement_floor=1022 burst=8 group_wait_ms=50 incident_wait_ms=50 incident_burst=2 types=feedback,melee,weapon_fire,throw,hit_death,grouped_sobject,grouped_reload,incident mutation=priority_only"
            : "ev=combat_feedback stage=install result=disabled reason=unsupported_layout_or_hook");

#endif
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=combat_feedback stage=install result=native hooks=0 compatibility=retired");
}

__declspec(noinline) inline std::int32_t __fastcall gather(
    void* lane, std::uint32_t channel, std::uint32_t laneIndex, const void* context,
    std::int32_t capacity, std::uint32_t* output) noexcept {
    const CallLease lease{};
    const auto native = originalGather.load(std::memory_order_acquire);
    if (native == nullptr) {
        return 0;
    }
    const auto result = native(lane, channel, laneIndex, context, capacity, output);
    const auto error = GetLastError();
    if (enabled.load(std::memory_order_acquire)) {
        const auto clock = entity_movement_fairness::read_clock();
        entity_movement_fairness::apply(lane, channel, laneIndex, capacity, result, output,
                                         GetTickCount64(), clock);
        const auto raised = entity_lifecycle_fairness::apply(lane, channel, laneIndex, capacity, result, output, clock);
        if (raised) removalsPromoted.fetch_add(raised, std::memory_order_relaxed);
        const auto created = entity_creation_fairness::apply(lane, channel, laneIndex, capacity, result, output, clock);
        if (created) creationsPromoted.fetch_add(created, std::memory_order_relaxed);
        const auto handed = entity_handoff_fairness::apply(lane, channel, laneIndex, capacity, result, output, clock);
        if (handed) handoffsPromoted.fetch_add(handed, std::memory_order_relaxed);
        const auto now = GetTickCount64();
        auto last = lastRemovalReport.load(std::memory_order_relaxed);
        if ((now < last || now - last >= 5000)
            && lastRemovalReport.compare_exchange_strong(last, now, std::memory_order_relaxed)) {
            const auto total = removalsPromoted.exchange(0, std::memory_order_relaxed);
            const auto creationTotal = creationsPromoted.exchange(0, std::memory_order_relaxed);
            const auto handoffTotal = handoffsPromoted.exchange(0, std::memory_order_relaxed);
            if (handoffTotal) {
                std::array<char,192> line{};
                const auto n = std::snprintf(line.data(), line.size(),
                    "ev=entity_handoff stage=gather promoted=%llu age_ms=250 burst=2 mutation=priority_only",
                    static_cast<unsigned long long>(handoffTotal));
                if (n > 0 && static_cast<std::size_t>(n) < line.size())
                    core::log::write(core::log::Channel::client, core::log::Level::info,
                        {line.data(), static_cast<std::size_t>(n)});
            }
            if (total) {
                std::array<char, 192> line{};
                const int n = std::snprintf(line.data(), line.size(),
                    "ev=entity_lifecycle stage=gather promoted=%llu burst=4 mutation=priority_only",
                    static_cast<unsigned long long>(total));
                if (n > 0 && static_cast<std::size_t>(n) < line.size())
                    core::log::write(core::log::Channel::client, core::log::Level::info,
                                     {line.data(), static_cast<std::size_t>(n)});
            }
            if (creationTotal) {
                std::array<char, 192> line{};
                const int n = std::snprintf(line.data(), line.size(),
                    "ev=entity_creation stage=gather promoted=%llu age_ms=250 burst=2 mutation=priority_only",
                    static_cast<unsigned long long>(creationTotal));
                if (n > 0 && static_cast<std::size_t>(n) < line.size())
                    core::log::write(core::log::Channel::client, core::log::Level::info,
                                     {line.data(), static_cast<std::size_t>(n)});
            }
        }
    }
    SetLastError(error);
    return result;
}

inline void install() noexcept {
#if 0

#if RESTORATION_NATIVE_COMBAT_PRIORITY
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=combat_feedback stage=install result=native detours=0 paired_signon_required=1");
#else
    install_feedback();
#endif
    if (gatherHandle.attached) {
        enabled.store(true, std::memory_order_release);
        return;
    }
    constexpr std::string_view pattern =
        "44 89 44 24 18 89 54 24 10 56 41 57 48 81 EC C8 00 00 00 33 F6 4C 89 49 18 4C 8B F9";
    const auto target = scan_main_image_unique(signature<signature_length(pattern)>(pattern),
                                                "entity_fairness.gather");
    const bool supported = target != nullptr && entity_movement_fairness::configure(target);
    if (supported && hooking::detour::install({target, reinterpret_cast<void*>(&gather)}, gatherHandle)) {
        originalGather.store(reinterpret_cast<Gather>(gatherHandle.original), std::memory_order_release);
        entity_lifecycle_fairness::configure(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
        entity_creation_fairness::configure(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
        entity_handoff_fairness::configure(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)));
        enabled.store(true, std::memory_order_release);
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
        gatherHandle.attached
            ? "ev=entity_fairness stage=install result=ok movement_ms=50 fallback_ms=250 priority_floor=1022 diagnostics_ms=5000 mutation=priority_only"
            : "ev=entity_fairness stage=install result=disabled reason=unsupported_layout_or_hook");
    core::log::write(core::log::Channel::client, core::log::Level::info, entity_lifecycle_fairness::imageBase.load(std::memory_order_acquire) != 0
        ? "ev=entity_lifecycle stage=install result=ok priority=1023 age_ms=50 burst=4 scope=native_local_pending_removals"
        : "ev=entity_lifecycle stage=install result=disabled reason=unsupported_layout_or_hook");
    core::log::write(core::log::Channel::client, core::log::Level::info,
        entity_handoff_fairness::configured.load(std::memory_order_acquire)
        ? "ev=entity_handoff stage=install result=ok priority=1023 age_ms=250 burst=2 scope=native_outgoing_handoff_groups"
        : "ev=entity_handoff stage=install result=disabled reason=unsupported_layout_or_hook");
    core::log::write(core::log::Channel::client, core::log::Level::info,
        entity_creation_fairness::configured.load(std::memory_order_acquire)
        ? "ev=entity_creation stage=install result=ok priority=1023 age_ms=250 burst=2 scope=native_local_creation_groups"
        : "ev=entity_creation stage=install result=disabled reason=unsupported_layout_or_hook");

#endif
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=entity_fairness stage=install result=native hooks=0 compatibility=retired");
}

inline bool uninstall() noexcept {
    enabled.store(false, std::memory_order_release);
    const std::array<hooking::detour::ProtectedCodeEntry, 2> protectedEntries{
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&gather)},
        hooking::detour::ProtectedCodeEntry{reinterpret_cast<void*>(&gather_events)}};
    if (eventGatherHandle.attached
        && hooking::detour::uninstall(eventGatherHandle, protectedEntries, &idle)
            != hooking::detour::UninstallResult::removed) return false;
    if (gatherHandle.attached) {
        if (hooking::detour::uninstall(gatherHandle, protectedEntries, &idle)
            != hooking::detour::UninstallResult::removed) {
            return false;
        }
    }
    originalGather.store(nullptr, std::memory_order_release);
    originalEventGather.store(nullptr, std::memory_order_release);
    combat_feedback_priority::imageBase.store(0, std::memory_order_release);
    entity_lifecycle_fairness::imageBase.store(0, std::memory_order_release);
    entity_creation_fairness::configured.store(false, std::memory_order_release);
    entity_creation_fairness::scanCursor = 0;
    entity_handoff_fairness::configured.store(false, std::memory_order_release);
    entity_handoff_fairness::scanCursor = 0;
    handoffsPromoted = 0;
    creationsPromoted = 0;
    removalsPromoted = 0; lastRemovalReport = 0;
    AcquireSRWLockExclusive(&combat_feedback_priority::statsLock);
    combat_feedback_priority::gathers = combat_feedback_priority::critical
        = combat_feedback_priority::promoted = combat_feedback_priority::maxAge
        = combat_feedback_priority::lastReport = 0;
    combat_feedback_priority::melee = combat_feedback_priority::grouped
        = combat_feedback_priority::deaths = combat_feedback_priority::meleeMaxAge = 0;
    combat_feedback_priority::incidents = combat_feedback_priority::incidentsPromoted
        = combat_feedback_priority::incidentMaxAge = 0;
    ReleaseSRWLockExclusive(&combat_feedback_priority::statsLock);
    entity_movement_fairness::table.store(nullptr, std::memory_order_release);
    entity_movement_fairness::clockRoot.store(nullptr, std::memory_order_release);
    entity_movement_fairness::sobjectPool.store(nullptr, std::memory_order_release);
    AcquireSRWLockExclusive(&entity_movement_fairness::lock);
    entity_movement_fairness::policy = {};
    entity_movement_fairness::observation = {};
    entity_movement_fairness::lastReport = 0;
    ReleaseSRWLockExclusive(&entity_movement_fairness::lock);
    return true;
}

} // namespace dawn::client::hooks::bootflow::entity_schedule_diagnostic
