#include <Windows.h>

#include <atomic>

#include "../../client/graphics/wine_compat.h"
#include "../../client/runtime/network_route_policy.h"
#include "../../client/hooks/package_trust/package_trust_bypass.h"
#include "../../client/runtime/runtime.h"
#include "../../core/logging/log.h"
#include "../../core/identity/local_ticket.h"
#include "../../core/runtime/core_runtime.h"
#include "../../core/runtime/host_environment.h"
#include "../../state/social/social_state.h"
#include "../../state/social/remote_profile_cache.h"
#include "callbacks/callback_registry.h"
#include "context/steam_context_state.h"
#include "internal.h"
#include "runtime.h"
#include "social/social_cache.h"
#include "social/realtime_client.h"

namespace dawn::steam {
namespace {

constexpr wchar_t kNetworkingModuleName[] = L"steamnetworkingsockets.dll";
SRWLOCK g_lifecycleLock{SRWLOCK_INIT};
std::atomic_bool g_initialized{false};
bool g_mainActivationDone{};
bool g_mainActivationResult{};
bool g_graphicsActivationAttempted{};
bool g_platformActivationAttempted{};

[[nodiscard]] HMODULE networking_caller_module(const void* callerAddress) noexcept {
    if (callerAddress == nullptr) {
        return nullptr;
    }
    HMODULE callerModule{};
    const DWORD flags =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(callerAddress), &callerModule) == FALSE
        || callerModule != GetModuleHandleW(kNetworkingModuleName)) {
        return nullptr;
    }
    return callerModule;
}

} // namespace

bool initialize(void* module) noexcept {
    if (core::runtime::is_wine()) {
        client::graphics::initialize_wine_display();
    }
    if (!client::runtime::network_route_policy::prepare()) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lifecycleLock);
    if (g_initialized.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return true;
    }
    if (!initialize_local_identity(module) || !core::initialize(module)) {
        shutdown_local_identity();
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return false;
    }
    if (!client::runtime::network_route_policy::initialize()) {
        (void)core::shutdown();
        shutdown_local_identity();
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return false;
    }
    // base generation (_0) packages register during bootload, before the first callback pump
    // runs the main-image hook sweep, so package trust must attach at Steam init
    if (!client::hooks::package_trust::install()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=steam_init stage=package_trust result=fail");
        (void)core::shutdown();
        shutdown_local_identity();
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return false;
    }
    context::advance_generation();
    g_initialized.store(true, std::memory_order_release);
    core::log::write(core::log::Channel::client, core::log::Level::info, "ev=steam_init result=ok");
    // the guard attaches above, before Core logging exists, so report its outcome here
    client::runtime::network_route_policy::report();
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    return true;
}

bool shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    const bool hadRuntime = g_initialized.load(std::memory_order_acquire) || core::is_initialized();
    if (!hadRuntime) {
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return true;
    }
    runtime::social::realtime_client::shutdown();
    runtime::social::shutdown();
    if (!core::shutdown()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=steam_shutdown stage=core result=fail");
        ReleaseSRWLockExclusive(&g_lifecycleLock);
        return false;
    }

    // release callback pointers only after Client hooks stop producing events
    runtime::callbacks::clear();
    state::social::remote_profile_cache::clear();
    state::social::clear();
    core::identity::local_ticket::shutdown();
    shutdown_local_identity();
    g_initialized.store(false, std::memory_order_release);
    g_mainActivationDone = false;
    g_mainActivationResult = false;
    g_graphicsActivationAttempted = false;
    g_platformActivationAttempted = false;
    context::advance_generation();
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    return true;
}

bool is_running() noexcept {
    return true;
}

} // namespace dawn::steam

namespace dawn::steam::runtime {

bool activate_main_once() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    if (!g_mainActivationDone && core::is_initialized()) {
        g_mainActivationDone = true;
        g_mainActivationResult = client::activate_main_once();
    }
    const bool result = g_mainActivationResult;
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    return result;
}

bool main_activation_pending() noexcept {
    AcquireSRWLockShared(&g_lifecycleLock);
    const bool pending = !g_mainActivationDone;
    ReleaseSRWLockShared(&g_lifecycleLock);
    // must match the Core test activation makes; otherwise a failed Core raises an overlay
    // no sweep ends
    return pending && core::is_initialized();
}

void activate_graphics_once() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    if (!g_graphicsActivationAttempted && core::is_initialized()) {
        g_graphicsActivationAttempted = true;
        (void)client::activate_graphics_once();
    }
    ReleaseSRWLockExclusive(&g_lifecycleLock);
}

void activate_platform_once(const void* callerAddress) noexcept {
    const HMODULE callerModule = networking_caller_module(callerAddress);
    if (callerModule == nullptr) {
        return;
    }

    AcquireSRWLockExclusive(&g_lifecycleLock);
    if (!g_platformActivationAttempted && g_initialized.load(std::memory_order_acquire)) {
        g_platformActivationAttempted = true;
        (void)client::activate_platform_once(callerModule);
    }
    ReleaseSRWLockExclusive(&g_lifecycleLock);
}

} // namespace dawn::steam::runtime
