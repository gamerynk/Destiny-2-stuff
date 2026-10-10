#include "investment_derived_rebuild.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "../../../../core/logging/log.h"
#include "../../../hooking/detour.h"
#include "internal.h"

namespace dawn::client::hooks::network::investment {
#if defined(RESTORATION_NATIVE_INVESTMENT_INVALIDATION)
namespace {
std::atomic_bool g_nativeInstalled{false};
}

void arm_derived_rebuild() noexcept {}

bool install() noexcept {
    g_nativeInstalled.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client, core::log::Level::info,
                     "ev=investment stage=install result=native_invalidation detours=0 variant=1");
    return true;
}

bool uninstall() noexcept {
    g_nativeInstalled.store(false, std::memory_order_release);
    return true;
}

bool is_installed() noexcept {
    return g_nativeInstalled.load(std::memory_order_acquire);
}

bool has_ownership() noexcept {
    return false;
}
#else
namespace {

// derived-state freshness verdict; only a rebuild recomputes the expiry, so a character
// cached before its replicated objects arrive never goes stale by itself
constexpr std::string_view kFreshnessSignatureText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 48 8B F8 0F B6 40 08 84 C0 74 ? 48 8B 53 18 "
    "48 8B 4B 08 E8 ? ? ? ?";
constexpr auto kFreshnessSignature =
    signature<signature_length(kFreshnessSignatureText)>(kFreshnessSignatureText);

// state-three family-four lookup call; a nonnull result proves the real object arrived
constexpr std::string_view kFamily4CallSignatureText =
    "48 8D 4B 10 E8 ? ? ? ? 48 8D B8 28 07 00 00 48 83 3F 00";
constexpr auto kFamily4CallSignature =
    signature<signature_length(kFamily4CallSignatureText)>(kFamily4CallSignatureText);

// offset of the call operand after the E8 near call in the family-four pattern
constexpr std::size_t kFamily4CallOperandOffset = 5;
constexpr std::size_t kNearCallOperandSize = 4;
constexpr std::size_t kFreshnessHandle = 0;
constexpr std::size_t kFamily4Handle = 1;
constexpr std::size_t kFamily4ResidentCapacity = 64;
constexpr char kStale = 0;

using Freshness = char(__fastcall*)(void*);
using Family4Lookup = void*(__fastcall*)(std::uint64_t*);

std::array<hooking::detour::Handle, 2> g_handles{};
std::atomic<Freshness> g_originalFreshness{nullptr};
std::atomic<Family4Lookup> g_originalFamily4Lookup{nullptr};
std::atomic_bool g_rebuildArmed{false};
std::atomic_uint32_t g_rebuildReports{0};
std::atomic_uint32_t g_family4Reports{0};
std::atomic_uint64_t g_family4Generation{0};

struct Family4Resident {
    std::atomic_uint64_t key{0};
    std::atomic_uintptr_t object{0};
};

std::array<Family4Resident, kFamily4ResidentCapacity> g_family4Residents{};

[[nodiscard]] bool any_primary_attached() noexcept {
    return g_handles[kFreshnessHandle].attached || g_handles[kFamily4Handle].attached;
}

void clear_runtime() noexcept {
    g_originalFreshness.store(nullptr, std::memory_order_release);
    g_originalFamily4Lookup.store(nullptr, std::memory_order_release);
    g_rebuildArmed.store(false, std::memory_order_release);
    g_rebuildReports.store(0, std::memory_order_release);
    g_family4Reports.store(0, std::memory_order_release);
    g_family4Generation.store(0, std::memory_order_release);
    for (Family4Resident& resident : g_family4Residents) {
        resident.object.store(0, std::memory_order_release);
        resident.key.store(0, std::memory_order_release);
    }
}

// QueueZ applies immutable snapshots by replacing the resident pointer; tracking each key
// avoids treating alternating lookups as state changes
[[nodiscard]] bool observe_family4_resident(std::uint64_t keyValue, void* resolved) noexcept {
    if (keyValue == 0 || resolved == nullptr) {
        return false;
    }
    const auto objectValue = reinterpret_cast<std::uintptr_t>(resolved);

    for (Family4Resident& resident : g_family4Residents) {
        if (resident.key.load(std::memory_order_acquire) == keyValue) {
            return resident.object.exchange(objectValue, std::memory_order_acq_rel) != objectValue;
        }
    }
    for (Family4Resident& resident : g_family4Residents) {
        std::uint64_t empty = 0;
        if (resident.key.compare_exchange_strong(empty,
                                                 keyValue,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
            resident.object.store(objectValue, std::memory_order_release);
            return true;
        }
        if (empty == keyValue) {
            return resident.object.exchange(objectValue, std::memory_order_acq_rel) != objectValue;
        }
    }
    return false;
}

__declspec(noinline) char __fastcall freshness(void* accessor) noexcept {
    if (g_rebuildArmed.exchange(false, std::memory_order_acq_rel)) {
        const std::uint32_t report = g_rebuildReports.fetch_add(1, std::memory_order_relaxed);
        if (report < 16) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             "ev=investment stage=derived result=rebuilt");
        }
        return kStale;
    }
    const Freshness original = g_originalFreshness.load(std::memory_order_acquire);
    return original != nullptr ? original(accessor) : kStale;
}

__declspec(noinline) void* __fastcall family4_lookup(std::uint64_t* key) noexcept {
    const Family4Lookup original = g_originalFamily4Lookup.load(std::memory_order_acquire);
    void* const resolved = original != nullptr ? original(key) : nullptr;
    const std::uint64_t keyValue = key != nullptr ? *key : 0;
    if (observe_family4_resident(keyValue, resolved)) {
        arm_derived_rebuild();
        const std::uint64_t generation =
            g_family4Generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        const std::uint32_t report = g_family4Reports.fetch_add(1, std::memory_order_relaxed);
        if (report < 16) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             "ev=investment stage=family4 result=resident generation="
                                 + std::to_string(generation));
        }
    }
    return resolved;
}

} // namespace

void arm_derived_rebuild() noexcept {
    g_rebuildArmed.store(true, std::memory_order_release);
}

bool install() noexcept {
    if (is_installed()) {
        return true;
    }
    if (has_ownership()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=ownership");
        return false;
    }

    std::byte* const freshnessTarget =
        scan_main_image_unique(kFreshnessSignature, "investment_derived_freshness");
    std::byte* const family4Call =
        scan_main_image_unique(kFamily4CallSignature, "queuez_family4_readiness_call");
    if (freshnessTarget == nullptr || family4Call == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=target");
        return false;
    }
    std::byte* const family4Target =
        resolve_relative(family4Call + kFamily4CallOperandOffset,
                         family4Call + kFamily4CallOperandOffset + kNearCallOperandSize);
    if (family4Target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=operand");
        return false;
    }

    const std::array specs{
        hooking::detour::Spec{freshnessTarget, reinterpret_cast<void*>(&freshness)},
        hooking::detour::Spec{family4Target, reinterpret_cast<void*>(&family4_lookup)},
    };
    std::array<hooking::detour::Handle, 2> installed{};
    if (!hooking::detour::install(specs, installed)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=install result=fail reason=attach");
        return false;
    }
    g_handles = installed;
    g_originalFreshness.store(reinterpret_cast<Freshness>(g_handles[kFreshnessHandle].original),
                              std::memory_order_release);
    g_originalFamily4Lookup.store(
        reinterpret_cast<Family4Lookup>(g_handles[kFamily4Handle].original),
        std::memory_order_release);

    if (!install_family5_rearm()) {
        if (hooking::detour::uninstall(g_handles)) {
            clear_runtime();
        } else {
            core::log::write(core::log::Channel::client,
                             core::log::Level::warn,
                             "ev=investment stage=install result=fail reason=rollback");
        }
        return false;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=investment stage=install result=ok");
    return true;
}

bool uninstall() noexcept {
    if (!uninstall_family5_rearm()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=uninstall result=fail reason=family5");
        return false;
    }
    if (any_primary_attached()
        && (!g_handles[kFreshnessHandle].attached || !g_handles[kFamily4Handle].attached
            || !hooking::detour::uninstall(g_handles))) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=uninstall result=fail reason=detach");
        return false;
    }
    clear_runtime();
    return true;
}

bool is_installed() noexcept {
    return g_handles[kFreshnessHandle].attached && g_handles[kFamily4Handle].attached
           && family5_rearm_is_installed();
}

bool has_ownership() noexcept {
    return any_primary_attached() || family5_rearm_is_installed();
}
#endif

} // namespace dawn::client::hooks::network::investment
