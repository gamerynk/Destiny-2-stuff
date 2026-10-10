#include "graphics_hook_lifecycle.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "../../../core/logging/log.h"
#include "../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../cursor/runtime.h"
#include "graphics_hook_replacements.h"
#include "native_window_presentation.h"

namespace dawn::client::hooks::graphics {

SRWLOCK g_hookLock{SRWLOCK_INIT};
std::atomic_uint g_activeHookCalls{};
thread_local std::size_t g_hookCallDepth{};
bool g_acceptRendererCalls{};
std::array<hooking::detour::Handle, kHandleCount> g_handles{};
std::array<void*, kHandleCount> g_targetEntries{};
Targets g_targets{};

namespace {

std::size_t g_requiredHandles = kHandleCount;
bool g_nativeRejected{};
bool g_fallbackPending{};

#if defined(RESTORATION_NATIVE_RESIZE_OMISSION) && RESTORATION_NATIVE_RESIZE_OMISSION
#if !defined(RESTORATION_TRANSIENT_RTV) || !RESTORATION_TRANSIENT_RTV
#error Native ResizeBuffers omission requires transient render targets in every renderer translation unit.
#endif
// native Present and resize enter from the same game-window command dispatcher
bool native_resize_owner_supported() noexcept {
    const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto matches = [base]<std::size_t N>(std::uintptr_t rva,
        const std::array<unsigned char, N>& expected) noexcept {
        std::array<unsigned char, N> bytes{};
        SIZE_T copied{};
        return base != 0 && ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<const void*>(base + rva), bytes.data(), N, &copied)
            && copied == N && bytes == expected;
    };
    // Tiger D3D Window class wndproc is at 0x3C0400 and forwards to 0x3BF770; both command
    // branches are in that procedure, and only this branch and its config helper call resize
    return matches(0x3C0400, std::array<unsigned char, 32>{
        0xE9,0x7A,0x5A,0x7D,0x07,0xE9,0xA0,0x48,0x03,0x04,0x8B,0x55,0x68,0x48,0x8B,0x4D,
        0x60,0x8B,0x49,0x04,0xE9,0x50,0x52,0x3D,0x08,0xE8,0x52,0xF3,0xFF,0xFF,0x90,0xEB})
        && matches(0x3BFE15, std::array<unsigned char, 14>{
            0x44,0x8B,0xC3,0x48,0x8B,0xD6,0x48,0x8B,0xCF,0xE8,0x4D,0x5E,0x00,0x00})
        && matches(0x3BFF48, std::array<unsigned char, 5>{0xE8,0xF3,0xCF,0xD5,0x00})
        && matches(0x111DF87, std::array<unsigned char, 22>{
            0x48,0x8B,0x0D,0xBA,0x99,0x6F,0x01,0x45,0x33,0xC0,0x8B,0xD3,0x48,0x8B,0xF0,
            0x4C,0x8B,0x09,0x41,0xFF,0x51,0x40})
        && matches(0x111E72F, std::array<unsigned char, 14>{
            0x4C,0x8B,0x19,0x8B,0xD7,0x44,0x89,0x54,0x24,0x20,0x41,0xFF,0x53,0x68});
}
#endif

[[nodiscard]] bool all_installed() noexcept {
    if (native_window::selected()) return g_acceptRendererCalls
        && std::none_of(g_handles.begin(), g_handles.end(),
            [](const hooking::detour::Handle& handle) { return handle.attached; });
    const auto boundary = g_handles.begin() + g_requiredHandles;
    return std::all_of(g_handles.begin(), boundary,
                      [](const hooking::detour::Handle& handle) { return handle.attached; })
        && std::none_of(boundary, g_handles.end(),
                        [](const hooking::detour::Handle& handle) { return handle.attached; });
}

[[nodiscard]] bool any_installed() noexcept {
    return std::any_of(g_handles.begin(),
                       g_handles.end(),
                       [](const hooking::detour::Handle& handle) { return handle.attached; });
}

bool install_detours_locked() noexcept {
    if (!discovery::resolve(g_targets)) return false;

    const replacement::EntryPoints entryPoints = replacement::entry_points();
    const std::array specs{
        hooking::detour::Spec{g_targets.present,
                              entryPoints[static_cast<std::size_t>(HookSlot::present)]},
        hooking::detour::Spec{g_targets.resizeBuffers,
                              entryPoints[static_cast<std::size_t>(HookSlot::resizeBuffers)]},
    };
    for (std::size_t index = 0; index < specs.size(); ++index) {
        g_targetEntries[index] = specs[index].target;
    }
    g_requiredHandles = kHandleCount;
#if defined(RESTORATION_NATIVE_RESIZE_OMISSION) && RESTORATION_NATIVE_RESIZE_OMISSION
    if (native_resize_owner_supported()) g_requiredHandles = 1;
#endif
    const bool installed = hooking::detour::install(std::span{specs}.first(g_requiredHandles),
        std::span{g_handles}.first(g_requiredHandles));
    if (installed) {
        g_acceptRendererCalls = true;
    } else {
        g_targetEntries = {};
        discovery::release(g_targets);
        g_requiredHandles = kHandleCount;
    }
    return installed;
}

} // namespace

bool install() noexcept {
    const core::ui::runtime::VisibilitySnapshot visibility = core::ui::runtime::snapshot();
    if (!visibility.initialized || !visibility.enabled) {
        return visibility.initialized;
    }

    AcquireSRWLockExclusive(&g_hookLock);
    if (all_installed()) {
        ReleaseSRWLockExclusive(&g_hookLock);
        return true;
    }
    if (!hook_calls_idle() || g_acceptRendererCalls || any_installed()
        || native_window::selected() || g_targets.present != nullptr) {
        ReleaseSRWLockExclusive(&g_hookLock);
        return false;
    }
    if (!g_nativeRejected && native_window::resolve()) {
        g_requiredHandles = 0;
        g_acceptRendererCalls = true;
        ReleaseSRWLockExclusive(&g_hookLock);
        core::log::write(core::log::Channel::client, core::log::Level::info,
            "ev=graphics stage=install result=ok hooks=0 presentation=native_window transient_rtv=1");
        return true;
    }
    const bool installed = install_detours_locked();
    const bool resizeOmitted = g_requiredHandles == 1;
    ReleaseSRWLockExclusive(&g_hookLock);
    if (installed) core::log::write(core::log::Channel::client, core::log::Level::info,
        resizeOmitted
            ? "ev=graphics stage=install result=ok hooks=1 resize=native_window_owner transient_rtv=1"
            : "ev=graphics stage=install result=ok hooks=2 resize=guarded");
    return installed;
}

bool uninstall() noexcept {
    if (current_thread_in_hook_call()) {
        // SRW locks cannot be upgraded recursively; caller retries after the SDK call unwinds
        return false;
    }
    AcquireSRWLockExclusive(&g_hookLock);
    g_fallbackPending = false;
    if (any_installed() && !all_installed()) {
        ReleaseSRWLockExclusive(&g_hookLock);
        renderer::dispatch_pending_input_release();
        return false;
    }
    if (!any_installed()) {
        g_acceptRendererCalls = false;
        if (!hook_calls_idle() || !renderer::shutdown() || !native_window::shutdown()) {
            ReleaseSRWLockExclusive(&g_hookLock);
            renderer::dispatch_pending_input_release();
            return false;
        }
    } else {
        g_acceptRendererCalls = false;
        if (!renderer::shutdown()) {
            // the attached Present stays available to send an owner-thread capture release
            g_acceptRendererCalls = true;
            ReleaseSRWLockExclusive(&g_hookLock);
            renderer::dispatch_pending_input_release();
            return false;
        }

        const replacement::ProtectedEntries protectedEntries = replacement::protected_entries();
        const hooking::detour::UninstallResult removal =
            hooking::detour::uninstall(g_handles, protectedEntries);
        if (removal == hooking::detour::UninstallResult::failed) {
            ReleaseSRWLockExclusive(&g_hookLock);
            renderer::dispatch_pending_input_release();
            return false;
        }
        if (removal == hooking::detour::UninstallResult::protectedCodeActive) {
            ReleaseSRWLockExclusive(&g_hookLock);
            renderer::dispatch_pending_input_release();
            return false;
        }
        if (!hook_calls_idle()) {
            ReleaseSRWLockExclusive(&g_hookLock);
            renderer::dispatch_pending_input_release();
            return false;
        }
    }

    if (g_acceptRendererCalls) {
        // a detached state must never publish renderer work into a later restart
        g_acceptRendererCalls = false;
    }
    if (!hook_calls_idle()) {
        ReleaseSRWLockExclusive(&g_hookLock);
        renderer::dispatch_pending_input_release();
        return false;
    }
    g_targetEntries = {};
    discovery::release(g_targets);
    g_requiredHandles = kHandleCount;
    g_nativeRejected = false;
    ReleaseSRWLockExclusive(&g_hookLock);
    renderer::dispatch_pending_input_release();
    return true;
}

void service_native_window() noexcept {
    if (current_thread_in_hook_call()) return;
    AcquireSRWLockExclusive(&g_hookLock);
    if (g_fallbackPending && !native_window::selected()) {
        const bool installed = hook_calls_idle() && install_detours_locked();
        if (installed) g_fallbackPending = false;
        ReleaseSRWLockExclusive(&g_hookLock);
        if (installed) core::log::write(core::log::Channel::client, core::log::Level::warn,
            "ev=graphics stage=native_fallback result=attached presentation=detour");
        return;
    }
    if (!native_window::selected() || !g_acceptRendererCalls) {
        ReleaseSRWLockExclusive(&g_hookLock);
        return;
    }
    if (!native_window::fallback_requested()) {
        // a window callback cannot retire its own binding; the worker retries after it unwinds
        // and never initializes COM/ImGui itself
        if (hook_calls_idle() && renderer::service_pending_shutdown())
            (void)native_window::service_bootstrap();
        ReleaseSRWLockExclusive(&g_hookLock);
        renderer::dispatch_pending_input_release();
        return;
    }
    if (!hook_calls_idle()) {
        ReleaseSRWLockExclusive(&g_hookLock);
        return;
    }
    // keep native metadata until the original window call and retired subclasses drain;
    // if cleanup fails the selected state stays retryable and cannot claim a detour fallback
    if (!cursor::prepare_native_tail_retirement()
        || !renderer::shutdown() || !native_window::shutdown()) {
        ReleaseSRWLockExclusive(&g_hookLock);
        renderer::dispatch_pending_input_release();
        return;
    }
    g_acceptRendererCalls = false;
    g_requiredHandles = kHandleCount;
    g_nativeRejected = true;
    g_fallbackPending = true;
    const bool installed = install_detours_locked();
    if (installed) g_fallbackPending = false;
    ReleaseSRWLockExclusive(&g_hookLock);
    core::log::write(core::log::Channel::client,
        installed ? core::log::Level::warn : core::log::Level::error,
        installed ? "ev=graphics stage=native_fallback result=attached presentation=detour"
                  : "ev=graphics stage=native_fallback result=retry presentation=unavailable");
}

bool is_installed() noexcept {
    AcquireSRWLockShared(&g_hookLock);
    const bool installed = all_installed();
    ReleaseSRWLockShared(&g_hookLock);
    return installed;
}

} // namespace dawn::client::hooks::graphics
