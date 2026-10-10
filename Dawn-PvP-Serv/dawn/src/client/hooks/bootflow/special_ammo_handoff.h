#pragma once
#ifndef RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL
#define RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL 0
#endif
#ifndef RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
#define RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL 0
#endif
#include <cmath>
#include <cstring>
#include "entity_movement_fairness.h"
#include "../../hooking/detour.h"
#include "pvp_native_query_component.h"

namespace dawn::client::hooks::bootflow::special_ammo_handoff {
namespace memory = entity_movement_fairness;
using Collect = void(__fastcall*)(void*, std::uint32_t) noexcept;
using Eligible = bool(__fastcall*)(void*, std::uint32_t) noexcept;
using Handoff = void(__fastcall*)(std::uint32_t) noexcept;
using Simulation = std::uint32_t(__fastcall*)(std::uint32_t) noexcept;
using AmmoQuery = void(__fastcall*)(void*, std::uint32_t, void*, void*) noexcept;
using Query = void(__fastcall*)(void*, std::uint32_t, std::uint8_t, void*,
    std::uintptr_t, const void*, const void*, const void*, bool, void*) noexcept;
using Transfer = bool(__fastcall*)(void*, std::uint32_t, int) noexcept;
inline constexpr std::size_t hookCount = RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL ? 0
    : (RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL ? 3 : 4);
inline std::array<hooking::detour::Handle,hookCount ? hookCount : 4> handles{};
inline std::atomic<Collect> original{};
inline std::atomic<Query> originalQuery{};
inline std::atomic<AmmoQuery> originalAmmoQuery{};
inline std::atomic<Transfer> originalTransfer{};
inline std::atomic<Simulation> nativeSimulation{};
inline std::atomic<Eligible> nativeEligible{};
inline std::atomic<Handoff> nativeHandoff{};
inline std::atomic<std::uintptr_t> imageBase{};
inline std::atomic_bool enabled{};
inline std::atomic<unsigned> activeCalls{};
inline std::atomic<std::uint64_t> requests{}, lastReport{};
inline std::atomic<std::uint64_t> qualified{}, deferred{};

struct Key {
    std::uint32_t legacy{UINT32_MAX}, root{UINT32_MAX}, player{UINT32_MAX},
                  body{UINT32_MAX}, simulation{UINT32_MAX};
};
struct Lease { Key key{}; std::uint64_t time{}; };
inline std::array<Lease,1024> leases{};
inline SRWLOCK leaseLock = SRWLOCK_INIT;
inline constexpr std::uint64_t leaseMs = 250;
struct QueryScope { void* result{}; std::uint32_t player{}; Key key{}; bool found{}; };
inline thread_local QueryScope* queryScope{};

// PC 1.59: grant E94C30 returns without authority and proximity can skip the grant, so
// request at the completed proximity query; defer only that handoff, never grant ammo
struct Retry { std::uint32_t legacy{UINT32_MAX}; std::uint64_t last{}; };
inline std::array<Retry, 4096> retries{};
inline SRWLOCK retryLock = SRWLOCK_INIT;
inline constexpr std::uint64_t retryMs = 250;

inline void report(std::uint64_t now) noexcept {
    auto last = lastReport.load(std::memory_order_relaxed);
    if (last && now - last < 5000) return;
    if (!lastReport.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    std::array<char,224> line{};
    const auto size = std::snprintf(line.data(), line.size(),
        "ev=special_ammo_handoff stage=runtime qualified=%llu requests=%llu deferred=%llu policy=proximity_lease_v2",
        static_cast<unsigned long long>(qualified.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(requests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(deferred.load(std::memory_order_relaxed)));
    if (size > 0 && static_cast<std::size_t>(size) < line.size())
        core::log::write(core::log::Channel::client, core::log::Level::info,
            {line.data(), static_cast<std::size_t>(size)});
}

inline bool due(std::uint32_t legacy, std::uint64_t now) noexcept {
    const auto index = legacy & 0x1FFFU;
    if (legacy == UINT32_MAX || index >= retries.size()) return false;
    AcquireSRWLockExclusive(&retryLock);
    auto& entry = retries[index];
    const bool allow = entry.legacy != legacy || now < entry.last || now - entry.last >= retryMs;
    if (allow) entry = {legacy, now};
    ReleaseSRWLockExclusive(&retryLock);
    return allow;
}

inline bool prepare(void* component, std::uint32_t player, std::uintptr_t image,
                    std::uint32_t& legacy, Key* key = nullptr) noexcept {
    if (!component || !image || player == UINT32_MAX) return false;
    const auto* module = reinterpret_cast<const std::byte*>(image);
    std::uint32_t type{}, stride{}, capacity{}, current{}, root{}, flags{}, unit{};
    std::uint8_t kind{}, overrideAmount{}, supported{};
    float amount{};
    if (!memory::read(component, 4, type) || type != 0x80804494U
        || !memory::read(component, 0x2D0, kind) || kind != 2
        || !memory::read(component, 0x2DC, overrideAmount) || overrideAmount != 1
        || !memory::read(component, 0x2DD, supported) || supported != 2
        || !memory::read(component, 0x2D8, amount) || !std::isfinite(amount) || amount <= 0
        || !memory::read(component, 0x2C, legacy) || legacy == UINT32_MAX) return false;
    const auto index = legacy & 0x1FFFU;
    const std::byte* units{};
    if (!memory::read(module, 0x1F93428, units) || !units
        || !memory::read(module, 0x1F93430, stride) || stride != 0xE0
        || !memory::read(module, 0x1F93434, capacity) || capacity != retries.size()
        || index >= capacity
        || !memory::read(units, index * stride + 0xC, current) || current != legacy
        || !memory::read(units, index * stride + 0x4C, root) || root == UINT32_MAX) return false;
    const std::byte* players{};
    const auto playerIndex = player & 0x1FFFU;
    if (!memory::read(module, 0x1F90E18, players) || !players
        || !memory::read(module, 0x1F90E20, stride) || stride != 480
        || !memory::read(module, 0x1F90E24, capacity) || !capacity || capacity > 256
        || playerIndex >= capacity
        || !memory::read(players, playerIndex * stride + 0x40, flags) || !(flags & 1U)
        || !memory::read(players, playerIndex * stride + 0x54, unit) || unit == UINT32_MAX) return false;
    if (key) {
        *key = {legacy, root, player, unit, UINT32_MAX};
        return true;
    }
    if (!memory::read(module, 0x26BE0E0 + (index >> 5) * 4, flags)
        || (flags & (1U << (index & 31)))) return false;
    return true;
}

inline bool current(const Key& key, std::uintptr_t image) noexcept {
    if (!image || key.legacy == UINT32_MAX || key.root == UINT32_MAX
        || key.player == UINT32_MAX || key.body == UINT32_MAX) return false;
    const auto* module = reinterpret_cast<const std::byte*>(image);
    const std::byte* units{}; const std::byte* players{};
    std::uint32_t stride{}, capacity{}, value{};
    const auto index = key.legacy & 0x1FFFU;
    const auto playerIndex = key.player & 0x1FFFU;
    return memory::read(module, 0x1F93428, units) && units
        && memory::read(module, 0x1F93430, stride) && stride == 0xE0
        && memory::read(module, 0x1F93434, capacity) && capacity == 4096 && index < capacity
        && memory::read(units, index * stride + 0xC, value) && value == key.legacy
        && memory::read(units, index * stride + 0x4C, value) && value == key.root
        && memory::read(module, 0x1F90E18, players) && players
        && memory::read(module, 0x1F90E20, stride) && stride == 480
        && memory::read(module, 0x1F90E24, capacity) && capacity && capacity <= 256 && playerIndex < capacity
        && memory::read(players, playerIndex * stride + 0x40, value) && (value & 1U)
        && memory::read(players, playerIndex * stride + 0x54, value) && value == key.body;
}

inline void record(const Key& key, bool eligible, std::uint64_t now) noexcept {
    const auto index = key.simulation & 0x1FFFU;
    if (key.simulation == UINT32_MAX || index >= leases.size()
        || !TryAcquireSRWLockExclusive(&leaseLock)) return;
    auto& entry = leases[index];
    if (eligible) entry = {key, now};
    else if (entry.key.simulation == key.simulation && entry.key.player == key.player) entry = {};
    ReleaseSRWLockExclusive(&leaseLock);
}

inline void __fastcall ammo_query(void* component, std::uint32_t player, void* context, void* result) noexcept {
    activeCalls.fetch_add(1, std::memory_order_acq_rel);
    const auto native = originalAmmoQuery.load(std::memory_order_acquire);
    if (native) native(component, player, context, result);
    const auto error = GetLastError();
    auto* scope = queryScope;
    const auto simulation = nativeSimulation.load(std::memory_order_acquire);
    std::uint32_t legacy{}; Key key{};
    if (native && enabled.load(std::memory_order_acquire) && simulation && scope
        && scope->result == result && scope->player == player
        && prepare(component, player, imageBase.load(std::memory_order_acquire), legacy, &key)) {
        key.simulation = simulation(legacy);
        scope->key = key;
        scope->found = key.simulation != UINT32_MAX && (key.simulation & 0x1FFFU) < leases.size();
    }
    activeCalls.fetch_sub(1, std::memory_order_acq_rel);
    SetLastError(error);
}

using MethodLookup = void(__fastcall*)(void*, const void*) noexcept;
inline std::atomic<MethodLookup> nativeMethodLookup{};
struct ResolvedQueryTrial {
    pvp_native_query_component::Snapshot snapshot{};
    Key key{};
    bool admitted{};
};
inline bool equal(const Key& left, const Key& right) noexcept {
    return left.legacy == right.legacy && left.root == right.root
        && left.player == right.player && left.body == right.body
        && left.simulation == right.simulation;
}
inline bool trial_snapshot(void* component, pvp_native_query_component::Snapshot& snapshot) noexcept {
    const auto image = imageBase.load(std::memory_order_acquire);
    const auto lookup = nativeMethodLookup.load(std::memory_order_acquire);
    std::array<std::byte,24> descriptor{};
    std::uint32_t schema{}, method{}, datum{}, mode{}, registeredClass{}, registeredCount{};
    std::uintptr_t registeredMethods{};
    if (!component || !lookup || !memory::read(component, 0x30, descriptor)) return false;
    std::memcpy(&schema, descriptor.data(), 4);
    std::memcpy(&method, descriptor.data() + 4, 4);
    std::memcpy(&datum, descriptor.data() + 8, 4);
    const auto* module = reinterpret_cast<const void*>(image);
    if (!memory::read(module, 0x241F7A0, mode) || mode != 0
        || !memory::read(module, 0x3280378, registeredClass) || registeredClass != 0x80804491U
        || !memory::read(module, 0x3280380, registeredMethods) || registeredMethods != image + 0x1CF64B0
        || !memory::read(module, 0x3280388, registeredCount) || registeredCount != 7) return false;
    if (schema != 0x80804491U || method != 3 || datum == UINT32_MAX) return false;
    std::array<std::uintptr_t,3> methodResult{};
    lookup(methodResult.data(), descriptor.data());
    const auto read = [](std::uintptr_t address, void* output, std::size_t size) noexcept {
        SIZE_T count{};
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
            output, size, &count) && count == size;
    };
    return pvp_native_query_component::resolve(image, reinterpret_cast<std::uintptr_t>(component),
        methodResult[1], read, snapshot)
        && snapshot.descriptor == descriptor;
}
inline ResolvedQueryTrial trial_begin(void* component, std::uint32_t player) noexcept {
    ResolvedQueryTrial trial{}; std::uint32_t legacy{};
    const auto simulation = nativeSimulation.load(std::memory_order_acquire);
    const auto image = imageBase.load(std::memory_order_acquire);
    if (!simulation || !trial_snapshot(component, trial.snapshot)
        || !prepare(reinterpret_cast<void*>(trial.snapshot.component), player, image, legacy, &trial.key))
        return trial;
    trial.key.simulation = simulation(legacy);
    trial.admitted = trial.key.simulation != UINT32_MAX
        && (trial.key.simulation & 0x1FFFU) < leases.size();
    return trial;
}
inline bool trial_finish(void* component, std::uint32_t player, const ResolvedQueryTrial& trial, Key& key) noexcept {
    if (!trial.admitted || player != trial.key.player) return false;
    pvp_native_query_component::Snapshot after{};
    const auto simulation = nativeSimulation.load(std::memory_order_acquire);
    const auto image = imageBase.load(std::memory_order_acquire);
    if (!simulation || !trial_snapshot(component, after) || after != trial.snapshot
        || !current(trial.key, image) || simulation(trial.key.legacy) != trial.key.simulation)
        return false;
    key = trial.key;
    return true;
}
inline void __fastcall query(void* component, std::uint32_t player, std::uint8_t flags, void* profile,
    std::uintptr_t movement, const void* position, const void* origin, const void* direction,
    bool update, void* result) noexcept {
    activeCalls.fetch_add(1, std::memory_order_acq_rel);
    const auto native = originalQuery.load(std::memory_order_acquire);
    QueryScope scope{result, player};
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
    const auto trialError = GetLastError();
    const auto trial = native && enabled.load(std::memory_order_acquire) && flags == 0 && update
        ? trial_begin(component, player) : ResolvedQueryTrial{};
    SetLastError(trialError);
#endif
    auto* previous = queryScope;
    queryScope = &scope;
    if (native) native(component, player, flags, profile, movement, position, origin, direction, update, result);
    const auto error = GetLastError();
    queryScope = previous;
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
    scope.found = trial_finish(component, player, trial, scope.key);
    SetLastError(error);
#endif
    if (native && enabled.load(std::memory_order_acquire) && scope.found && flags == 0 && update) {
        std::uint8_t state{}, automatic{};
        const auto image = imageBase.load(std::memory_order_acquire);
        const auto now = GetTickCount64();
        const bool accepted = memory::read(result, 1, state) && state == 2
            && memory::read(result, 0xC, automatic) && automatic
            && current(scope.key, image);
        record(scope.key, accepted, now);
        if (accepted) {
            qualified.fetch_add(1, std::memory_order_relaxed);
            std::uint32_t bits{};
            const auto index = scope.key.legacy & 0x1FFFU;
            const auto handoff = nativeHandoff.load(std::memory_order_acquire);
            if (handoff && memory::read(reinterpret_cast<const void*>(image),
                    0x26BE0E0 + (index >> 5) * 4, bits)
                && !(bits & (1U << (index & 31))) && due(scope.key.legacy, now)) {
                handoff(scope.key.legacy);
                requests.fetch_add(1, std::memory_order_relaxed);
            }
            report(now);
        }
    }
    activeCalls.fetch_sub(1, std::memory_order_acq_rel);
    SetLastError(error);
}

inline bool pending(void* manager, std::uint32_t token, std::uint64_t now) noexcept {
    const auto image = imageBase.load(std::memory_order_acquire);
    if (!manager || !image || token == UINT32_MAX) return false;
    const auto tokenIndex = token & 0x1FFFU;
    std::uint32_t allocated{}, stored{}, simulation{};
    std::int16_t slot{}; std::uint8_t kind{}, owner{};
    if (!memory::read(manager, 0xC520 + (tokenIndex >> 5) * 4, allocated)
        || !(allocated & (1U << (tokenIndex & 31)))
        || !memory::read(manager, 0x114 + tokenIndex * 6, slot) || slot < 0 || slot >= 1024) return false;
    const auto* row = reinterpret_cast<const std::byte*>(image + 0x30B0440) + slot * 0x70;
    if (!memory::read(row, 0, kind) || kind != 0
        || !memory::read(row, 1, owner) || owner != 0xFF
        || !memory::read(row, 8, stored) || stored != token
        || !memory::read(row, 4, simulation) || simulation == UINT32_MAX
        || (simulation & 0x1FFFU) >= leases.size()
        || !TryAcquireSRWLockShared(&leaseLock)) return false;
    const auto entry = leases[simulation & 0x1FFFU];
    ReleaseSRWLockShared(&leaseLock);
    return entry.key.simulation == simulation && now >= entry.time && now - entry.time < leaseMs
        && current(entry.key, image);
}

inline bool __fastcall transfer(void* manager, std::uint32_t token, int peer) noexcept {
    activeCalls.fetch_add(1, std::memory_order_acq_rel);
    const auto native = originalTransfer.load(std::memory_order_acquire);
    const auto incomingError = GetLastError();
    const bool hold = native && enabled.load(std::memory_order_acquire) && peer >= 0 && peer < 32
        && pending(manager, token, GetTickCount64());
    SetLastError(incomingError);
    const bool result = hold ? false : (native && native(manager, token, peer));
    const auto error = GetLastError();
    if (hold) deferred.fetch_add(1, std::memory_order_relaxed);
    activeCalls.fetch_sub(1, std::memory_order_acq_rel);
    SetLastError(error);
    return result;
}

inline bool request(void* component, std::uint32_t player, std::uint64_t now) noexcept {
    const auto eligible = nativeEligible.load(std::memory_order_acquire);
    const auto handoff = nativeHandoff.load(std::memory_order_acquire);
    std::uint32_t legacy{};
    if (!eligible || !handoff
        || !prepare(component, player, imageBase.load(std::memory_order_acquire), legacy)
        || !eligible(component, player) || !due(legacy, now)) return false;
    handoff(legacy);
    requests.fetch_add(1, std::memory_order_relaxed);
    return true;
}

inline void __fastcall collect(void* component, std::uint32_t player) noexcept {
    activeCalls.fetch_add(1, std::memory_order_acq_rel);
    const auto native = original.load(std::memory_order_acquire);
    const auto incomingError = GetLastError();
    bool requested = false;
    if (native && enabled.load(std::memory_order_acquire))
        requested = request(component, player, GetTickCount64());
    SetLastError(incomingError);
    if (native) native(component, player);
    const auto error = GetLastError();
    if (requested) report(GetTickCount64());
    activeCalls.fetch_sub(1, std::memory_order_acq_rel);
    SetLastError(error);
}

template<std::size_t N> inline bool matches(const void* image, std::size_t offset,
    const std::array<std::uint8_t, N>& expected) noexcept {
    std::array<std::uint8_t, N> actual{};
    return memory::read(image, offset, actual) && actual == expected;
}
inline bool supported(const void* image) noexcept {
    std::uintptr_t callback{}; std::uint32_t symbol{};
    return image
        && matches(image, 0xE94C30, std::array<std::uint8_t,15>{0x4C,0x8B,0xDC,0x56,0x41,0x54,0x41,0x55,0x48,0x81,0xEC,0xA0,0,0,0})
        && matches(image, 0xE94C7B, std::array<std::uint8_t,12>{0x8B,0x04,0x82,0x0F,0xA3,0xC8,0x0F,0x83,0x82,0x02,0,0})
        && matches(image, 0xE8B030, std::array<std::uint8_t,16>{0x40,0x57,0x48,0x83,0xEC,0x50,0x48,0x8B,0x05,0xAB,0x80,0x1F,0x01,0x48,0x8B,0xF9})
        && matches(image, 0x403A80, std::array<std::uint8_t,17>{0x40,0x53,0x48,0x83,0xEC,0x30,0x8B,0xD9,0xE8,0x33,0x3C,0xFF,0xFF,0xF6,0x00,0x02,0x74})
        && matches(image, 0xD342C2, std::array<std::uint8_t,7>{0x8B,0xCE,0xE8,0xB7,0xF7,0x6C,0xFF})
        && matches(image, 0xF32CD0, std::array<std::uint8_t,16>{0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57})
        && matches(image, 0xF33066, std::array<std::uint8_t,9>{0x41,0xC6,0x47,0x01,0,0x49,0x8D,0x6F,0x10})
        && matches(image, 0xE8A870, std::array<std::uint8_t,5>{0x48,0x89,0x5C,0x24,0x08})
        && matches(image, 0x170CAA0, std::array<std::uint8_t,19>{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xB0,0,0,0})
        && matches(image, 0x403ED0, std::array<std::uint8_t,11>{0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9,0x83,0xF9,0xFF})
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
        && matches(image, 0xA40F40, std::array<std::uint8_t,16>{0x8B,0x02,0x89,0x01,0x8B,0x42,0x04,0x89,0x41,0x04,0xE9,0xC1,0xF2,0xFF,0xFF,0xCC})
#endif
        && memory::read(image, 0x1CF64E0, callback)
        && callback == reinterpret_cast<std::uintptr_t>(image) + 0xE8A870
        && memory::read(image, 0x1CF64E8, symbol) && symbol == 0xA1873B0C
        && memory::read(image, 0x1CF64F0, callback)
        && callback == reinterpret_cast<std::uintptr_t>(image) + 0xE94C30
        && memory::read(image, 0x1CF64F8, symbol) && symbol == 0x445966AD;
}
inline void install_for_image(std::byte* image) noexcept {
#if RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL
    (void)image;
    bool attached = false;
    for (const auto& handle : handles) attached = attached || handle.attached;
    if (attached || enabled.load(std::memory_order_acquire)
        || activeCalls.load(std::memory_order_acquire) != 0
        || original.load(std::memory_order_acquire)
        || originalQuery.load(std::memory_order_acquire)
        || originalAmmoQuery.load(std::memory_order_acquire)
        || originalTransfer.load(std::memory_order_acquire)
        || nativeEligible.load(std::memory_order_acquire)
        || nativeSimulation.load(std::memory_order_acquire)
        || nativeHandoff.load(std::memory_order_acquire)
        || nativeMethodLookup.load(std::memory_order_acquire)
        || imageBase.load(std::memory_order_acquire) != 0) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
            "ev=special_ammo_handoff stage=install result=refused reason=preexisting_ammo_state native_trial=1");
        return;
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=special_ammo_handoff stage=install result=native_trial hooks=0 unvalidated=1");
#else
    if (handles[0].attached) return;
    std::array<hooking::detour::Spec,hookCount> specs{};
    specs[0] = {image + 0xE94C30, reinterpret_cast<void*>(&collect)};
    specs[1] = {image + 0xF32CD0, reinterpret_cast<void*>(&query)};
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
    specs[2] = {image + 0x170CAA0, reinterpret_cast<void*>(&transfer)};
#else
    specs[2] = {image + 0xE8A870, reinterpret_cast<void*>(&ammo_query)};
    specs[3] = {image + 0x170CAA0, reinterpret_cast<void*>(&transfer)};
#endif
    if (supported(image) && hooking::detour::install(specs, handles)) {
        original.store(reinterpret_cast<Collect>(handles[0].original), std::memory_order_release);
        originalQuery.store(reinterpret_cast<Query>(handles[1].original), std::memory_order_release);
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
        originalAmmoQuery.store(nullptr, std::memory_order_release);
        originalTransfer.store(reinterpret_cast<Transfer>(handles[2].original), std::memory_order_release);
        nativeMethodLookup.store(reinterpret_cast<MethodLookup>(image + 0xA40F40), std::memory_order_release);
#else
        originalAmmoQuery.store(reinterpret_cast<AmmoQuery>(handles[2].original), std::memory_order_release);
        originalTransfer.store(reinterpret_cast<Transfer>(handles[3].original), std::memory_order_release);
#endif
        nativeSimulation.store(reinterpret_cast<Simulation>(image + 0x403ED0), std::memory_order_release);
        nativeEligible.store(reinterpret_cast<Eligible>(image + 0xE8B030), std::memory_order_release);
        nativeHandoff.store(reinterpret_cast<Handoff>(image + 0x403A80), std::memory_order_release);
        imageBase.store(reinterpret_cast<std::uintptr_t>(image), std::memory_order_release);
        enabled.store(true, std::memory_order_release);
    }
    core::log::write(core::log::Channel::client, core::log::Level::info, handles[0].attached
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
        ? "ev=special_ammo_handoff stage=install result=ok policy=native_descriptor_trial hooks=3 unvalidated=1"
#else
        ? "ev=special_ammo_handoff stage=install result=ok policy=proximity_lease_v2 lease_ms=250"
#endif
        : "ev=special_ammo_handoff stage=install result=disabled reason=unsupported_layout_or_hook");
#endif
}
inline void install() noexcept {
    install_for_image(reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr)));
}
inline bool uninstall() noexcept {
#if RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL
    bool attached = false;
    for (const auto& handle : handles) attached = attached || handle.attached;
    if (attached || activeCalls.load(std::memory_order_acquire) != 0) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
            "ev=special_ammo_handoff stage=uninstall result=refused reason=preexisting_ammo_owner native_trial=1");
        return false;
    }
#endif
    enabled.store(false, std::memory_order_release);
#if !RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL
    if (handles[0].attached) {
        std::array<hooking::detour::ProtectedCodeEntry,hookCount> entries{};
        entries[0] = {reinterpret_cast<void*>(&collect)};
        entries[1] = {reinterpret_cast<void*>(&query)};
#if RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL
        entries[2] = {reinterpret_cast<void*>(&transfer)};
#else
        entries[2] = {reinterpret_cast<void*>(&ammo_query)};
        entries[3] = {reinterpret_cast<void*>(&transfer)};
#endif
        if (hooking::detour::uninstall(handles, entries,
            +[]() noexcept { return activeCalls.load(std::memory_order_acquire) == 0; })
            != hooking::detour::UninstallResult::removed) {
            enabled.store(true, std::memory_order_release); return false;
        }
    }
#endif
    nativeMethodLookup = nullptr;
    original = nullptr; nativeEligible = nullptr; nativeHandoff = nullptr; imageBase = 0;
    originalQuery = nullptr; originalAmmoQuery = nullptr; originalTransfer = nullptr; nativeSimulation = nullptr;
    requests = 0; lastReport = 0; qualified = 0; deferred = 0;
    AcquireSRWLockExclusive(&retryLock);
    retries = {};
    ReleaseSRWLockExclusive(&retryLock);
    AcquireSRWLockExclusive(&leaseLock); leases = {}; ReleaseSRWLockExclusive(&leaseLock);
    return true;
}
}
