#include "native_cursor_tail_runtime.h"
#include "native_cursor_pending_tail.h"
#include "runtime.h"
#include "cursor_guard_replacements.h"
#include "../polled_input/runtime.h"
#include "../graphics/input/input.h"
#include "../graphics/native_window_presentation.h"
#include "../../diagnostics/module_range.h"
#include "../../../core/ui/runtime/ui_visibility_runtime.h"
#include "../../../core/logging/log.h"
#include <array>
#include <atomic>
#include <cstring>

#ifndef RESTORATION_NATIVE_CLIP_OMISSION
#define RESTORATION_NATIVE_CLIP_OMISSION 0
#endif

namespace dawn::client::hooks::cursor::tail {
namespace {
namespace input = graphics::input;
namespace ui = core::ui::runtime;
pending_tail::Policy g_policy;
std::atomic_uintptr_t g_image{};
std::atomic_bool g_used{}, g_ready{}, g_stopping{}, g_retired{true};
std::atomic_bool g_reopen{};
std::atomic_bool g_suspending{}, g_bindingRetired{};
std::atomic_bool g_rejected{};
thread_local unsigned g_depth{};

bool memory(std::uintptr_t address, std::size_t size, bool executable) noexcept {
    if (!address || size > UINTPTR_MAX - address) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (VirtualQuery(reinterpret_cast<void*>(address), &m, sizeof m) != sizeof m
        || m.State != MEM_COMMIT || (m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
        || address + size > reinterpret_cast<std::uintptr_t>(m.BaseAddress) + m.RegionSize) return false;
    return !executable || (m.Type == MEM_IMAGE && (m.Protect & (PAGE_EXECUTE_READ
        | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0);
}
bool native_state(std::uintptr_t image, pending_tail::State& s) noexcept {
    __try {
        s.nativeWindow = *reinterpret_cast<const HWND*>(image + 0x26869C8);
        s.marker = *reinterpret_cast<const volatile unsigned*>(image + 0x2686998);
        return *reinterpret_cast<const volatile unsigned*>(image + 0x24218E0) == 1
            && s.marker <= 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_basic(void*, pending_tail::State& s) noexcept {
    const auto v = ui::snapshot();
    const auto main = input::snapshot_main_input();
    s.visibilityGeneration = v.generation;
    s.visibilityAvailable = v.initialized && v.enabled && v.generation;
    s.visible = v.visible;
    s.mainBindingGeneration = main.bindingGeneration;
    s.windowRevision = main.revision;
    s.window = main.window; s.windowThread = main.thread;
    s.mainBindingOwned = main.installed;
    s.primaryPauseOwned = polled_input::native_pause_owned();
    return s.visibilityAvailable && main.installed;
}
bool read_state(void* context, pending_tail::State& s) noexcept {
    (void)read_basic(context, s);
    const auto main = input::snapshot_main_input();
    const auto image = g_image.load(std::memory_order_acquire);
    s.nativeContractQualified = image && native_state(image, s)
        && polled_input::native_pause_available() && main.installed
        && IsWindow(main.window) != FALSE
        && static_cast<std::uintptr_t>(GetClassLongPtrW(main.window, GCLP_WNDPROC)) == image + 0x3C0400;
    return s.nativeContractQualified;
}
bool claim(void*, std::uint64_t generation, std::uint64_t binding, pending_tail::Lease& lease) noexcept {
    return input::claim_raw_opening(generation, binding, lease);
}
pending_tail::LeaseSnapshot raw(void*, const pending_tail::Lease& lease) noexcept {
    return input::snapshot_raw_opening(lease);
}
bool release(void*, const pending_tail::Lease& lease) noexcept { return input::release_raw_opening(lease); }
input::RawInputSnapshot raw_binding(void*) noexcept { return input::snapshot_raw_input(); }
bool clear(void*) noexcept {
    const auto image = g_image.load(std::memory_order_acquire);
    if (!image || !polled_input::native_key_recovery_safe()) return false;
    __try {
        const auto initialized = *reinterpret_cast<const volatile unsigned*>(image + 0x24218E0);
        if (initialized == 0) return true;
        if (initialized != 1) return false;
        const auto id = *reinterpret_cast<const unsigned*>(image + 0x241F994);
        const auto arena = *reinterpret_cast<const std::uintptr_t*>(image + 0x20D4AB8);
        const auto record = arena + 0x35A8 + (id & 0x3FFFFFU) * 64;
        if (!arena || !memory(record, 64, false)) return false;
        const auto dependency = *reinterpret_cast<const unsigned*>(record + 8);
        if (dependency != UINT32_MAX) {
            const auto list = *reinterpret_cast<const std::uintptr_t*>(arena + 0x20);
            const auto rows = *reinterpret_cast<const std::uintptr_t*>(list + 8);
            if (*reinterpret_cast<const unsigned*>(rows + (dependency & 0xFFFFU) * 0xB8 + 0x70) != 0)
                return false;
        }
        reinterpret_cast<void(__fastcall*)()>(image + 0x344E20)();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uint64_t open(void*, UINT key, std::uint64_t hidden) noexcept {
    ui::VisibilitySnapshot opened{};
    return ui::open_for_key(key, hidden, opened) ? opened.generation : 0;
}
bool close(void*, UINT, std::uint64_t visible) noexcept {
    ui::VisibilitySnapshot closed{};
    return ui::close_generation(visible, closed);
}
void apply(void*, bool visible) noexcept {
    // a selected detach can fail and keep the real API guard, so publish its modal answer
    // before the tail's ClipCursor call or the guard would suppress the fresh close rectangle
    publish_guarded_visibility(visible);
    polled_input::apply_visibility(visible);
}
bool geometry(void*, HWND window, RECT& rectangle) noexcept {
    if (!GetClientRect(window, &rectangle)) return false;
    POINT a{rectangle.left, rectangle.top}, b{rectangle.right, rectangle.bottom};
    if (!ClientToScreen(window, &a) || !ClientToScreen(window, &b)) return false;
    rectangle = {a.x, a.y, b.x, b.y};
    return rectangle.left <= rectangle.right && rectangle.top <= rectangle.bottom;
}
bool clip(void*, const RECT* rectangle) noexcept { return ::ClipCursor(rectangle) != FALSE; }
void fallback(void*) noexcept {
    g_ready.store(false, std::memory_order_release);
    g_rejected.store(true, std::memory_order_release);
    request_native_clip_guard();
}
void guarded_visibility(void*, bool visible) noexcept {
    publish_guarded_visibility(visible);
    apply(nullptr, visible);
}
bool guard_attached(void*) noexcept { return native_clip_guard_attached(); }
pending_tail::Owner owner(HWND window) noexcept {
    return {window, GetCurrentThreadId(), g_depth == 1
        && !graphics::native_window::in_preparation() && polled_input::native_key_recovery_safe()};
}
}

bool configured() noexcept { return g_policy.configured(); }
bool manages_policy() noexcept { return g_used.load(std::memory_order_acquire); }
bool omission_ready() noexcept {
    return configured() && !g_stopping.load(std::memory_order_acquire)
        && g_ready.load(std::memory_order_acquire);
}
void bootstrap() noexcept {
    if (!RESTORATION_NATIVE_CLIP_OMISSION || configured() || g_rejected.load(std::memory_order_acquire)
        || g_stopping.load(std::memory_order_acquire)
        || !polled_input::native_pause_available()) return;
    diagnostics::ModuleRange range{};
    if (!diagnostics::module_range(GetModuleHandleW(nullptr), range)) return;
    const auto match = [&](std::uintptr_t rva, const auto& bytes) noexcept {
        return rva <= range.end - range.base && bytes.size() <= range.end - range.base - rva
            && memory(range.base + rva, bytes.size(), true)
            && std::memcmp(reinterpret_cast<const void*>(range.base + rva), bytes.data(), bytes.size()) == 0;
    };
    if (!match(0x344E20, std::array<unsigned char, 48>{
        0x48,0x83,0xEC,0x38,0x48,0x8B,0x05,0x5D,0x4C,0xD6,0x01,0x48,0x33,0xC4,
        0x48,0x89,0x44,0x24,0x28,0x8B,0x05,0xA7,0xCA,0x0D,0x02,0xF0,0x83,0x0C,
        0x24,0x00,0x83,0xF8,0x01,0x75,0x76,0x8B,0x15,0x4B,0xAB,0x0D,0x02,0x48,
        0x8D,0x4C,0x24,0x20,0x48,0x89})
        || !match(0x3BBF28, std::array<unsigned char, 17>{
            0xFF,0x15,0x12,0xF2,0xCF,0x01,0xB8,0x01,0x00,0x00,0x00,0x87,0x05,0x5F,0xAA,0x2C,0x02})
        || !match(0x3C5F7E, std::array<unsigned char, 12>{
            0xFF,0x15,0xBC,0x51,0xCF,0x01,0x87,0x35,0x0E,0x0A,0x2C,0x02}))
        return;
    g_image.store(range.base, std::memory_order_release);
    pending_tail::Bindings b{nullptr, read_state, read_basic, claim, raw, release, raw_binding, clear, open,
        close, apply, geometry, clip, fallback, guarded_visibility, guard_attached};
    if (g_policy.configure(true, b)) {
        g_used.store(true, std::memory_order_release); g_retired.store(false, std::memory_order_release);
        core::log::write(core::log::Channel::client, core::log::Level::info,
            "ev=cursor stage=native_tail result=configured capture=pre_visible policy=marker");
    }
}
bool request_toggle(HWND window, UINT key) noexcept {
    if (!configured()) return false;
    const auto main = input::snapshot_main_input();
    if (!main.installed || main.window != window || main.thread != GetCurrentThreadId()) {
        fallback(nullptr); return true;
    }
    (void)g_policy.request_toggle(key);
    return true;
}
bool prepare_shutdown() noexcept {
    if (!configured()) return g_retired.load(std::memory_order_acquire);
    g_stopping.store(true, std::memory_order_release); g_ready.store(false, std::memory_order_release);
    return g_retired.load(std::memory_order_acquire);
}
bool prepare_retirement() noexcept {
    if (!configured()) return true;
    g_ready.store(false, std::memory_order_release);
    g_suspending.store(true, std::memory_order_release);
    const auto v = ui::snapshot();
    if (!v.visible && !g_policy.pending() && !g_policy.owns_capture())
        g_bindingRetired.store(true, std::memory_order_release);
    return g_bindingRetired.load(std::memory_order_acquire);
}
void binding_started() noexcept {
    g_suspending.store(false, std::memory_order_release);
    g_bindingRetired.store(false, std::memory_order_release);
}
void reset_after_uninstall() noexcept {
    if (configured() || g_policy.owns_capture() || !g_retired.load(std::memory_order_acquire)) return;
    g_image.store(0, std::memory_order_release);
    g_used.store(false, std::memory_order_release);
    g_ready.store(false, std::memory_order_release);
    g_stopping.store(false, std::memory_order_release);
    g_rejected.store(false, std::memory_order_release);
    g_reopen.store(false, std::memory_order_release);
    g_suspending.store(false, std::memory_order_release);
    g_bindingRetired.store(false, std::memory_order_release);
}

OwnerScope::OwnerScope(HWND window, UINT message) noexcept : window_(window) {
    ++g_depth; outer_ = g_depth == 1;
    if (!configured() || !outer_ || (message != WM_DESTROY && message != WM_NCDESTROY)) return;
    const auto v = ui::snapshot();
    g_reopen.store(v.visible || g_policy.pending(), std::memory_order_release);
    const auto currentOwner = owner(window);
    if (!g_policy.suspend_window(currentOwner)) {
        if (v.visible) { ui::VisibilitySnapshot hidden{}; (void)ui::close_generation(v.generation, hidden); }
        fallback(nullptr);
    }
    g_ready.store(false, std::memory_order_release);
}
OwnerScope::~OwnerScope() {
    const DWORD error = GetLastError();
    if (configured() && outer_) {
        const auto currentOwner = owner(window_);
        if (currentOwner.outsideNativeOwnership) {
            pending_tail::State state{};
            if (g_rejected.load(std::memory_order_acquire)) {
                if (native_clip_guard_attached() && read_basic(nullptr, state)
                    && state.window == window_) {
                    const bool wanted = !g_stopping.load(std::memory_order_acquire)
                        && (g_policy.intent_open() || g_reopen.load(std::memory_order_acquire));
                    const auto before = ui::snapshot();
                    if (!wanted && before.visible) {
                        ui::VisibilitySnapshot hidden{};
                        (void)ui::close_generation(before.generation, hidden);
                    }
                    if (g_policy.handoff_guarded(currentOwner, wanted)) {
                        g_used.store(false, std::memory_order_release);
                        g_retired.store(true, std::memory_order_release);
                        g_reopen.store(false, std::memory_order_release);
                        auto v = ui::snapshot();
                        if (wanted && !v.visible) {
                            ui::VisibilitySnapshot opened{};
                            (void)ui::open_for_key(v.toggleVirtualKey, v.generation, opened);
                        } else if (!wanted && v.visible) {
                            ui::VisibilitySnapshot closed{};
                            (void)ui::close_generation(v.generation, closed);
                        }
                        v = ui::snapshot();
                        apply(nullptr, v.visible);
                        restore_guarded_policy(v.visible);
                        core::log::write(core::log::Channel::client, core::log::Level::info,
                            "ev=cursor stage=native_tail result=guarded_handoff toggles=legacy capture=released");
                    }
                }
            } else if (read_state(nullptr, state) && state.window == window_) {
                if (g_stopping.load(std::memory_order_acquire)) {
                    (void)g_policy.suspend_window(currentOwner);
                    if (g_policy.shutdown(currentOwner)) {
                        g_retired.store(true, std::memory_order_release);
                        g_ready.store(false, std::memory_order_release);
                    }
                } else if (g_suspending.load(std::memory_order_acquire)) {
                    if (state.visible || g_policy.pending()) g_reopen.store(true, std::memory_order_release);
                    if (g_policy.suspend_window(currentOwner)) {
                        const auto result = g_policy.observe_owner(currentOwner);
                        if ((result == pending_tail::Result::closed || result == pending_tail::Result::idle
                             || result == pending_tail::Result::cancelled) && !g_policy.owns_capture())
                            g_bindingRetired.store(true, std::memory_order_release);
                    }
                    g_ready.store(false, std::memory_order_release);
                } else {
                    if (!state.visible && polled_input::native_pause_owned()) apply(nullptr, false);
                    const auto result = g_policy.observe_owner(currentOwner);
                    const auto rawState = input::snapshot_raw_input();
                    if ((result == pending_tail::Result::closed || result == pending_tail::Result::idle
                         || result == pending_tail::Result::cancelled) && rawState.installed
                         && g_reopen.exchange(false)) {
                        (void)g_policy.request_toggle(ui::snapshot().toggleVirtualKey);
                        (void)g_policy.observe_owner(currentOwner);
                    }
                    const bool accepted = result == pending_tail::Result::idle
                        || result == pending_tail::Result::opened || result == pending_tail::Result::released
                        || result == pending_tail::Result::closed || result == pending_tail::Result::cancelled
                        || result == pending_tail::Result::waitingDrain;
                    const bool ready = accepted && !g_rejected.load(std::memory_order_acquire)
                        && rawState.installed && read_state(nullptr, state)
                        && state.window == window_ && state.mainBindingOwned;
                    g_ready.store(ready, std::memory_order_release);
                }
            } else fallback(nullptr);
        }
    }
    if (g_depth) --g_depth;
    SetLastError(error);
}
}
