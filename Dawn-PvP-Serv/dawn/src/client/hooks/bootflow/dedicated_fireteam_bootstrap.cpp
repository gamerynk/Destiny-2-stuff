#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../../../core/logging/log.h"
#include "../../../core/runtime/runtime_policy.h"
#include "../../../steam/runtime/runtime.h"
#include "../../../steam/runtime/social/realtime_client.h"
#include "../../hooking/detour.h"
#include "dedicated_fireteam_bootstrap_policy.h"
#include "native_fireteam_callback.h"
#include "native_fireteam_callback_policy.h"
#include "../../diagnostics/module_range.h"
#include "internal.h"
#include "bootflow_hook_lifecycle.h"
#include "../../player/player_position.h"

namespace dawn::client::hooks::bootflow {
namespace {

namespace policy = dedicated_fireteam_bootstrap_policy;
namespace realtime = steam::runtime::social::realtime_client;
namespace callback_policy = native_fireteam_callback_policy;

constexpr std::uintptr_t kManagerSelectionPumpRva = 0x0175E520U;
// PC 1.59 logical PRIMARY accessor for one of the three group-session lanes
constexpr std::uintptr_t kPrimarySessionAccessorRva = 0x00C26490U;
constexpr std::uintptr_t kOwnDescriptorGetterRva = 0x0177A120U;
// native GroupSession::state dword, read by retail task 8 before its local-host fallback
constexpr std::size_t kSessionStateOffset = 0x1AEF8U;
constexpr std::array<std::byte, 20> kManagerSelectionPumpPrefix{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5C}, std::byte{0x24}, std::byte{0x08},
    std::byte{0x48}, std::byte{0x89}, std::byte{0x74}, std::byte{0x24}, std::byte{0x10},
    std::byte{0x48}, std::byte{0x89}, std::byte{0x7C}, std::byte{0x24}, std::byte{0x18},
    std::byte{0x55}, std::byte{0x41}, std::byte{0x54}, std::byte{0x41}, std::byte{0x55}};
using ManagerSelectionPump = void(__fastcall*)() noexcept;
using PrimarySessionAccessor = std::byte*(__fastcall*)(std::int32_t group) noexcept;
using NativeOwnDescriptorGetter = bool(__fastcall*)(std::byte* session,
                                                     std::byte* descriptor) noexcept;
hooking::detour::Handle g_handle{};
SRWLOCK g_captureLifetime = SRWLOCK_INIT;
std::atomic_bool g_nativeCallbackSelected{};
std::atomic_bool g_compatibilityObserverSelected{};
std::uintptr_t g_callbackImageBase{};
std::size_t g_callbackImageSize{};
thread_local callback_policy::CallbackDepth g_callbackDepth{};
std::atomic<ManagerSelectionPump> g_original{nullptr};
std::atomic<PrimarySessionAccessor> g_primarySessionAccessor{nullptr};
std::atomic<NativeOwnDescriptorGetter> g_nativeOwnDescriptorGetter{nullptr};
std::atomic_bool g_nativeDependenciesValidated{};
std::atomic_bool g_prearmReported{};
std::atomic_uint64_t g_lastDescriptorLobby{};
std::atomic_uint64_t g_lastDescriptorHash{};
std::atomic_bool g_descriptorGetterFailureReported{};

// exact opening of PC 1.59's own-descriptor getter: clears the caller's 128-byte result, then
// reads the group session's net-session handle at +0x1C7C0
constexpr std::array<std::byte, 28> kOwnDescriptorGetterPrefix{
    std::byte{0x48}, std::byte{0x89}, std::byte{0x5C}, std::byte{0x24}, std::byte{0x08},
    std::byte{0x57}, std::byte{0x48}, std::byte{0x83}, std::byte{0xEC}, std::byte{0x20},
    std::byte{0x48}, std::byte{0x8B}, std::byte{0xFA}, std::byte{0x48}, std::byte{0x8B},
    std::byte{0xD9}, std::byte{0x48}, std::byte{0x8B}, std::byte{0xCF}, std::byte{0x33},
    std::byte{0xD2}, std::byte{0x41}, std::byte{0xB8}, std::byte{0x80}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xE8}};

template <std::size_t Size>
[[nodiscard]] bool matches(const std::byte* target,
                           const std::array<std::byte, Size>& expected) noexcept {
    if (target == nullptr) {
        return false;
    }
    __try {
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (target[index] != expected[index]) {
                return false;
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <std::size_t Size>
[[nodiscard]] std::byte* exact_target(std::byte* image,
                                      std::uintptr_t rva,
                                      const std::array<std::byte, Size>& prefix) noexcept {
    if (image == nullptr) {
        return nullptr;
    }
    std::byte* const target = image + rva;
    return matches(target, prefix) ? target : nullptr;
}

[[nodiscard]] std::byte* primary_accessor_target(std::byte* image) noexcept {
    if (image == nullptr) {
        return nullptr;
    }
    std::byte* const target = image + kPrimarySessionAccessorRva;
    std::array<std::byte, policy::kPrimaryAccessorSignatureSize> code{};
    __try {
        for (std::size_t index = 0; index < code.size(); ++index) {
            code[index] = target[index];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return policy::primary_accessor_is_pc_1_59(code) ? target : nullptr;
}

template <typename Value>
[[nodiscard]] Value read_unaligned(const std::byte* source) noexcept {
    Value value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

void reset_descriptor_cache_capture() noexcept {
    g_descriptorGetterFailureReported.store(false, std::memory_order_release);
}

[[nodiscard]] bool own_descriptor_getter_candidate(const std::byte* code) noexcept {
    if (!matches(code, kOwnDescriptorGetterPrefix)) {
        return false;
    }
    const auto relative_call_target = [code](std::size_t callOffset) noexcept {
        return code + callOffset + 5U
               + read_unaligned<std::int32_t>(code + callOffset + 1U);
    };
    if (relative_call_target(27U)
            != code + (0x0187E862U - kOwnDescriptorGetterRva)
        || relative_call_target(49U)
               != code + (0x0179AF00U - kOwnDescriptorGetterRva)) {
        return false;
    }

    constexpr std::array<std::byte, 17> kHandleResolvePrefix{
        std::byte{0x8B}, std::byte{0x9B}, std::byte{0xC0}, std::byte{0xC7},
        std::byte{0x01}, std::byte{0x00}, std::byte{0x83}, std::byte{0xFB},
        std::byte{0xFF}, std::byte{0x0F}, std::byte{0x84}, std::byte{0x82},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x8B},
        std::byte{0xCB}};
    constexpr std::array<std::byte, 23> kReadyGate{
        std::byte{0x48}, std::byte{0x8B}, std::byte{0xC8}, std::byte{0x83},
        std::byte{0xFB}, std::byte{0xFF}, std::byte{0x74}, std::byte{0x73},
        std::byte{0x8B}, std::byte{0x40}, std::byte{0x04}, std::byte{0xC1},
        std::byte{0xE8}, std::byte{0x04}, std::byte{0xA8}, std::byte{0x01},
        std::byte{0x74}, std::byte{0x69}, std::byte{0x48}, std::byte{0x85},
        std::byte{0xFF}, std::byte{0x74}, std::byte{0x57}};
    if (!matches(code + 32U, kHandleResolvePrefix)
        || code[49U] != std::byte{0xE8}
        || !matches(code + 54U, kReadyGate)) {
        return false;
    }

    constexpr std::array<std::size_t, 8> kLoadOffsets{77U, 87U, 98U, 109U,
                                                       120U, 131U, 142U, 153U};
    constexpr std::array<std::size_t, 8> kStoreOffsets{84U, 94U, 105U, 116U,
                                                        127U, 138U, 149U, 160U};
    constexpr std::int32_t kNetSessionDescriptorOffset = 0x57C;
    for (std::size_t index = 0; index < kLoadOffsets.size(); ++index) {
        const std::size_t load = kLoadOffsets[index];
        const std::size_t store = kStoreOffsets[index];
        const std::byte expectedLoadModRm = index % 2U == 0U
                                                ? std::byte{0x81}
                                                : std::byte{0x89};
        if (code[load] != std::byte{0x0F} || code[load + 1U] != std::byte{0x10}
            || code[load + 2U] != expectedLoadModRm
            || read_unaligned<std::int32_t>(code + load + 3U)
                   != kNetSessionDescriptorOffset
                          + static_cast<std::int32_t>(index * 16U)
            || code[store] != std::byte{0x0F} || code[store + 1U] != std::byte{0x11}) {
            return false;
        }
        if (index == 0U) {
            if (code[store + 2U] != std::byte{0x07}) {
                return false;
            }
        } else {
            const std::byte expectedStoreModRm = index % 2U == 0U
                                                     ? std::byte{0x47}
                                                     : std::byte{0x4F};
            if (code[store + 2U] != expectedStoreModRm
                || code[store + 3U] != static_cast<std::byte>(index * 16U)) {
                return false;
            }
        }
    }
    constexpr std::array<std::byte, 26> kEpilogues{
        std::byte{0xB0}, std::byte{0x01}, std::byte{0x48}, std::byte{0x8B},
        std::byte{0x5C}, std::byte{0x24}, std::byte{0x30}, std::byte{0x48},
        std::byte{0x83}, std::byte{0xC4}, std::byte{0x20}, std::byte{0x5F},
        std::byte{0xC3}, std::byte{0x32}, std::byte{0xC0}, std::byte{0x48},
        std::byte{0x8B}, std::byte{0x5C}, std::byte{0x24}, std::byte{0x30},
        std::byte{0x48}, std::byte{0x83}, std::byte{0xC4}, std::byte{0x20},
        std::byte{0x5F}, std::byte{0xC3}};
    return matches(code + 164U, kEpilogues);
}

[[nodiscard]] std::byte* own_descriptor_getter_target(std::byte* image) noexcept {
    if (image == nullptr) {
        return nullptr;
    }
    __try {
        std::byte* const target = image + kOwnDescriptorGetterRva;
        return own_descriptor_getter_candidate(target) ? target : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

[[nodiscard]] std::int32_t primary_state() noexcept {
    constexpr std::int32_t kInvalidState = (std::numeric_limits<std::int32_t>::min)();
    const PrimarySessionAccessor accessor =
        g_primarySessionAccessor.load(std::memory_order_acquire);
    if (accessor == nullptr) {
        return kInvalidState;
    }
    std::byte* session = nullptr;
    __try {
        session = accessor(0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        session = nullptr;
    }
    if (session == nullptr) {
        return kInvalidState;
    }
    __try {
        return *reinterpret_cast<const std::int32_t*>(session + kSessionStateOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return kInvalidState;
    }
}

void capture_native_owner_descriptor() noexcept {
    if (!core::runtime::policy::kDedicatedBackendRequired
        || !g_nativeDependenciesValidated.load(std::memory_order_acquire)
        || primary_state() != 6) {
        return;
    }
    const std::uint64_t localPlatformId = steam::local_steam_id();
    if (localPlatformId == 0U) {
        return;
    }
    const PrimarySessionAccessor accessor =
        g_primarySessionAccessor.load(std::memory_order_acquire);
    NativeOwnDescriptorGetter getter =
        g_nativeOwnDescriptorGetter.load(std::memory_order_acquire);
    if (getter == nullptr) {
        auto* const image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
        std::byte* const getterTarget = own_descriptor_getter_target(image);
        if (getterTarget != nullptr) {
            getter = reinterpret_cast<NativeOwnDescriptorGetter>(getterTarget);
            g_nativeOwnDescriptorGetter.store(getter, std::memory_order_release);
        }
    }
    std::array<std::byte, middleware::gameplay::descriptor::kDescriptorSize> descriptor{};
    bool captured = false;
    std::byte* session = nullptr;
    __try {
        session = accessor != nullptr ? accessor(0) : nullptr;
        captured = session != nullptr && getter != nullptr
                   && getter(session, descriptor.data());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        captured = false;
    }
    std::uint64_t nativeLobbyId = 0U;
    if (captured) {
        std::memcpy(&nativeLobbyId, descriptor.data() + 110U, sizeof(nativeLobbyId));
    }
    const auto lobby = realtime::lobby_snapshot(nativeLobbyId);
    if (!captured || lobby.id == 0U || lobby.ownerPlatformId != localPlatformId
        || !policy::descriptor_is_native_owner(descriptor, lobby.id)) {
        descriptor = {};
        if (!g_descriptorGetterFailureReported.exchange(true, std::memory_order_acq_rel)) {
            std::array<char, 224> line{};
            const int written = std::snprintf(
                line.data(),
                line.size(),
                "ev=bootflow stage=native_fireteam_descriptor_capture result=idle reason=awaiting_native_owner_descriptor lobby=0x%016llX getter_rva=0x%08llX mutation=none",
                static_cast<unsigned long long>(lobby.id),
                static_cast<unsigned long long>(getter != nullptr
                                                    ? kOwnDescriptorGetterRva
                                                    : 0U));
            if (written > 0) {
                core::log::write(
                    core::log::Channel::client,
                    core::log::Level::info,
                    {line.data(),
                     (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
            }
        }
        return;
    }
    g_descriptorGetterFailureReported.store(false, std::memory_order_release);
    std::uint64_t machineId = 0;
    std::memcpy(&machineId, descriptor.data(), sizeof(machineId));
    if (!captured || !policy::descriptor_is_native_owner(descriptor, lobby.id)
        || !realtime::stage_native_fireteam_descriptor(lobby.id, descriptor)) {
        return;
    }
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte value : descriptor) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    const bool newValue = g_lastDescriptorLobby.exchange(lobby.id) != lobby.id
                          || g_lastDescriptorHash.exchange(hash) != hash;
    if (newValue) {
        std::array<char, 320> line{};
        const int written = std::snprintf(
            line.data(), line.size(),
            "ev=bootflow stage=native_fireteam_descriptor_capture result=staged lobby=0x%016llX machine=0x%016llX method=%u bytes=%zu owner_state=6 source=%s mutation=none",
            static_cast<unsigned long long>(lobby.id),
            static_cast<unsigned long long>(machineId),
            static_cast<unsigned>(std::to_integer<std::uint8_t>(descriptor[93U])),
            descriptor.size(),
            "retail_group_zero_getter");
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(written),
                                         line.size() - 1U)});
        }
    }
}

void capture_owner_descriptor_guarded() noexcept {
    AcquireSRWLockShared(&g_captureLifetime);
#if defined(RESTORATION_NATIVE_CAMERA_OBSERVERS) && RESTORATION_NATIVE_CAMERA_OBSERVERS
    poll_world_step();
    client::player::position::poll();
#endif
    capture_native_owner_descriptor();
    ReleaseSRWLockShared(&g_captureLifetime);
}

void __fastcall manager_selection_pump() noexcept {
    const ManagerSelectionPump original = g_original.load(std::memory_order_acquire);
    if (original != nullptr) original();
    capture_owner_descriptor_guarded();
}

[[maybe_unused, nodiscard]] bool native_callback_frame_validated(std::byte* image) noexcept {
    client::diagnostics::ModuleRange range{};
    if (!client::diagnostics::module_range(reinterpret_cast<HMODULE>(image), range)
        || range.base != reinterpret_cast<std::uintptr_t>(image)
        || range.end <= range.base
        || range.end - range.base < callback_policy::kOrderedCallsRva + callback_policy::kOrderedCalls.size())
        return false;
    constexpr std::array<std::byte, 6> prolog{
        std::byte{0x40}, std::byte{0x53}, std::byte{0x48},
        std::byte{0x83}, std::byte{0xEC}, std::byte{0x20}};
    constexpr std::array<std::byte, 6> callbackImport{
        std::byte{0xFF}, std::byte{0x15}, std::byte{0xB4},
        std::byte{0x2F}, std::byte{0xC7}, std::byte{0x01}};
    __try {
        if (!matches(image + callback_policy::kFrameRva, prolog)
            || !matches(image + 0x44850EU, callbackImport)
            || !callback_policy::ordered_calls_match(
                {image + callback_policy::kOrderedCallsRva,
                 callback_policy::kOrderedCalls.size()})) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    g_callbackImageBase = range.base;
    g_callbackImageSize = range.end - range.base;
    return true;
}

void reset_capture_state() noexcept {
    g_prearmReported.store(false, std::memory_order_release);
    g_lastDescriptorLobby.store(0, std::memory_order_release);
    g_lastDescriptorHash.store(0, std::memory_order_release);
    reset_descriptor_cache_capture();
}

} // namespace

void begin_native_fireteam_callback() noexcept { g_callbackDepth.begin(); }
void end_native_fireteam_callback() noexcept { g_callbackDepth.end(); }
bool native_fireteam_callback_is_outermost() noexcept { return g_callbackDepth.is_outermost(); }

bool native_camera_observers_available() noexcept {
#if defined(RESTORATION_NATIVE_CAMERA_OBSERVERS) && RESTORATION_NATIVE_CAMERA_OBSERVERS
    return g_nativeDependenciesValidated.load(std::memory_order_acquire)
           && (g_nativeCallbackSelected.load(std::memory_order_acquire)
               || g_compatibilityObserverSelected.load(std::memory_order_acquire));
#else
    return false;
#endif
}

void service_native_fireteam_callback() noexcept {
    if (!g_nativeCallbackSelected.load(std::memory_order_acquire)
        || g_callbackDepth.depth != 1 || g_callbackDepth.captured) return;
    std::array<void*, 16> addresses{};
    const USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(addresses.size()),
                                              addresses.data(), nullptr);
    std::array<std::uintptr_t, 16> frames{};
    for (USHORT index = 0; index < count; ++index)
        frames[index] = reinterpret_cast<std::uintptr_t>(addresses[index]);
    AcquireSRWLockShared(&g_captureLifetime);
    if (g_nativeCallbackSelected.load(std::memory_order_acquire)
        && callback_policy::owns_network_frame({frames.data(), count},
                                               g_callbackImageBase, g_callbackImageSize)
        && g_callbackDepth.admit()) {
#if defined(RESTORATION_NATIVE_CAMERA_OBSERVERS) && RESTORATION_NATIVE_CAMERA_OBSERVERS
        poll_world_step();
        client::player::position::poll();
#endif
        capture_native_owner_descriptor();
    }
    ReleaseSRWLockShared(&g_captureLifetime);
}

bool prearm_dedicated_fireteam_bootstrap() noexcept {
    if (!core::runtime::policy::kDedicatedBackendRequired) {
        return true;
    }
    const bool validated = g_nativeDependenciesValidated.load(std::memory_order_acquire);
    const bool first = !g_prearmReported.exchange(true, std::memory_order_acq_rel);
    if (first) {
        core::log::write(
            core::log::Channel::client,
            core::log::Level::info,
            "ev=bootflow stage=native_fireteam_descriptor_capture result=idle reason=awaiting_owner_group_zero mutation=none");
    }
    return validated;
}

bool install_dedicated_fireteam_bootstrap() noexcept {
    if (!core::runtime::policy::kDedicatedBackendRequired) {
        return true;
    }
    if (g_handle.attached || g_nativeCallbackSelected.load(std::memory_order_acquire)) {
        return g_nativeDependenciesValidated.load(std::memory_order_acquire);
    }

    auto* const image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    std::byte* const pumpTarget =
        exact_target(image, kManagerSelectionPumpRva, kManagerSelectionPumpPrefix);
    std::byte* const accessorTarget = primary_accessor_target(image);
    std::byte* const getterTarget = own_descriptor_getter_target(image);
    if (pumpTarget == nullptr || accessorTarget == nullptr) {
        std::array<char, 224> line{};
        const int written = std::snprintf(
            line.data(),
            line.size(),
            "ev=bootflow stage=native_fireteam_descriptor_capture result=disabled reason=native_dependency_signature pump=%u accessor=%u getter=%u fallback=retail",
            pumpTarget != nullptr ? 1U : 0U,
            accessorTarget != nullptr ? 1U : 0U,
            getterTarget != nullptr ? 1U : 0U);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::error,
                             {line.data(),
                              (std::min)(static_cast<std::size_t>(written),
                                         line.size() - 1U)});
        }
        return false;
    }

    g_primarySessionAccessor.store(
        reinterpret_cast<PrimarySessionAccessor>(accessorTarget), std::memory_order_release);
    g_nativeOwnDescriptorGetter.store(
        reinterpret_cast<NativeOwnDescriptorGetter>(getterTarget), std::memory_order_release);
#if defined(RESTORATION_NATIVE_FIRETEAM_CALLBACK) && RESTORATION_NATIVE_FIRETEAM_CALLBACK
    if (getterTarget != nullptr && native_callback_frame_validated(image)) {
        reset_capture_state();
        g_nativeDependenciesValidated.store(true, std::memory_order_release);
        g_nativeCallbackSelected.store(true, std::memory_order_release);
        core::log::write(core::log::Channel::client, core::log::Level::info,
            "ev=bootflow stage=native_fireteam_descriptor_capture result=native detours=0 source=network_frame_callbacks after_manager=1 mutation=none");
        return true;
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=bootflow stage=native_fireteam_descriptor_capture result=fallback reason=callback_frame_signature detours=1");
#endif
    if (!hooking::detour::install(
            {pumpTarget, reinterpret_cast<void*>(&manager_selection_pump)}, g_handle)) {
        g_primarySessionAccessor.store(nullptr, std::memory_order_release);
        g_nativeOwnDescriptorGetter.store(nullptr, std::memory_order_release);
        core::log::write(
            core::log::Channel::client,
            core::log::Level::error,
            "ev=bootflow stage=native_fireteam_descriptor_capture result=disabled reason=attach fallback=retail");
        return false;
    }

    g_original.store(
        reinterpret_cast<ManagerSelectionPump>(g_handle.original), std::memory_order_release);
    reset_capture_state();
    g_nativeDependenciesValidated.store(true, std::memory_order_release);
    g_compatibilityObserverSelected.store(true, std::memory_order_release);
    std::array<char, 256> line{};
    const int written = std::snprintf(
        line.data(), line.size(),
        "ev=bootflow stage=native_fireteam_descriptor_capture result=ok getter_rva=0x%08llX source=%s mutation=none",
        static_cast<unsigned long long>(getterTarget != nullptr ? getterTarget - image : 0U),
        getterTarget != nullptr ? "retail_group_zero_getter"
                                : "retail_group_zero_getter_deferred");
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), (std::min)(static_cast<std::size_t>(written),
                                                  line.size() - 1U)});
    }
    return true;
}

void uninstall_dedicated_fireteam_bootstrap() noexcept {
    if (g_handle.attached && !hooking::detour::uninstall(g_handle)) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
            "ev=bootflow stage=native_fireteam_descriptor_capture result=retained reason=detach");
        return;
    }
    g_nativeCallbackSelected.store(false, std::memory_order_release);
    g_compatibilityObserverSelected.store(false, std::memory_order_release);
    AcquireSRWLockExclusive(&g_captureLifetime);
    g_callbackImageBase = 0;
    g_callbackImageSize = 0;
    g_original.store(nullptr, std::memory_order_release);
    g_primarySessionAccessor.store(nullptr, std::memory_order_release);
    g_nativeOwnDescriptorGetter.store(nullptr, std::memory_order_release);
    g_nativeDependenciesValidated.store(false, std::memory_order_release);
    reset_capture_state();
    ReleaseSRWLockExclusive(&g_captureLifetime);
}

} // namespace dawn::client::hooks::bootflow
