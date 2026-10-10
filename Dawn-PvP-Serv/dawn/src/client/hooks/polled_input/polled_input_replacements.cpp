#include "polled_input_replacements.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "../../../core/logging/log.h"
#include "../../diagnostics/module_range.h"
#include "runtime.h"
#include "native_key_focus_policy.h"
#include "../graphics/native_window_presentation.h"

namespace dawn::client::hooks::polled_input {
namespace {

std::atomic_bool g_interfaceOpen{};
diagnostics::ModuleRange g_gameRange{};
using SetPrimaryInput = void(__fastcall*)(bool);
struct NativePauseRoute {
    SetPrimaryInput setter{};
    const std::uint8_t* primary{};
    const std::uint8_t* controller{};
    std::uint8_t savedPrimary{};
    bool owned{};
    const volatile std::uint32_t* focus{};
    std::uint32_t observedFocus{};
    HWND keyWindow{};
    const volatile std::uint8_t* keyWindowFocused{};
    const HWND* keyWindowAddress{};
};
NativePauseRoute g_nativePause{};
SRWLOCK g_nativePauseLock = SRWLOCK_INIT;
std::atomic_bool g_nativePauseOwned{};
native_key::Policy g_nativeKeyFocus{};

void refresh_native_focus_locked() noexcept {
    if (!g_nativePause.owned || g_nativePause.focus == nullptr) return;
    const auto focus = *g_nativePause.focus;
    const auto primary = *g_nativePause.primary;
    const HWND keyWindow = g_nativePause.keyWindowAddress != nullptr
        ? *g_nativePause.keyWindowAddress : g_nativePause.keyWindow;
    if (keyWindow != nullptr
        && (*g_nativePause.keyWindowFocused == 0
            || GetForegroundWindow() != keyWindow)) {
        // closing from a foreign teardown thread must never enable real background input
        g_nativePause.savedPrimary = 0;
        g_nativePause.observedFocus = focus;
        if (primary != 0) g_nativePause.setter(false);
        return;
    }
    if (focus == g_nativePause.observedFocus && primary == 0) return;
    // native focus publication supersedes the state saved before the interface opened; on
    // focus gain the original window procedure enabled input, so pause it again
    g_nativePause.savedPrimary = focus != g_nativePause.observedFocus
        ? (focus != 0 ? 1 : 0) : primary;
    g_nativePause.observedFocus = focus;
    if (primary != 0) g_nativePause.setter(false);
}

// the controller policy byte is only observed; the native setter owns raw mouse clearing
// and its own mutex
void apply_native_pause(bool visible) noexcept {
    AcquireSRWLockExclusive(&g_nativePauseLock);
    if (g_nativePause.setter != nullptr) {
        if (visible && !g_nativePause.owned) {
            g_nativePause.savedPrimary = *g_nativePause.primary;
            if (g_nativePause.focus != nullptr) g_nativePause.observedFocus = *g_nativePause.focus;
            g_nativePause.setter(false);
            g_nativePause.owned = true;
            g_nativePauseOwned.store(true, std::memory_order_release);
        } else if (!visible && g_nativePause.owned) {
            refresh_native_focus_locked();
            g_nativePause.setter(g_nativePause.savedPrimary != 0);
            g_nativePause.owned = false;
            g_nativePauseOwned.store(false, std::memory_order_release);
        }
    }
    ReleaseSRWLockExclusive(&g_nativePauseLock);

}

bool native_pause_range(std::uintptr_t address, std::size_t size, bool executable) noexcept {
    if (address < g_gameRange.base || address > g_gameRange.end
        || size > g_gameRange.end - address) return false;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof memory) != sizeof memory
        || memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
        || address + size > reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize)
        return false;
    return !executable || (memory.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
        | PAGE_EXECUTE_WRITECOPY)) != 0;
}

void resolve_native_pause() noexcept {
#if defined(RESTORATION_NATIVE_INPUT_PAUSE) && RESTORATION_NATIVE_INPUT_PAUSE
    constexpr std::uintptr_t setterRva = 0x3441B0;
    constexpr std::uintptr_t primaryRva = 0x24218E4;
    constexpr std::uintptr_t controllerRva = 0x24218E5;
    // native 3BE970 exchanges this dword right before calling 3441B0
    constexpr std::uintptr_t focusRva = 0x2686994;
    constexpr std::uintptr_t focusPublisherRva = 0x3BE9C4;
    constexpr std::array<std::uint8_t, 15> expectedFocusPublisher{
        0x87,0x0D,0xCA,0x7F,0x2C,0x02,0x40,0x0F,0xB6,0xCD,0xE8,0xDD,0x57,0xF8,0xFF};
    constexpr std::array<std::uint8_t, 29> expected{
        0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0x05,0xCB,0x58,0xD6,0x01,
        0x48,0x33,0xC4,0x48,0x89,0x44,0x24,0x28,0x88,0x0D,0x19,0xD7,0x0D,0x02,0x84,0xC9};
    const auto setter = g_gameRange.base + setterRva;
    const auto primary = g_gameRange.base + primaryRva;
    const auto controller = g_gameRange.base + controllerRva;
    const auto focus = g_gameRange.base + focusRva;
    const auto focusPublisher = g_gameRange.base + focusPublisherRva;
    const bool valid = native_pause_range(setter, expected.size(), true)
        && native_pause_range(primary, 2, false)
        && native_pause_range(focus, sizeof(std::uint32_t), false)
        && native_pause_range(focusPublisher, expectedFocusPublisher.size(), true)
        && std::memcmp(reinterpret_cast<const void*>(setter), expected.data(), expected.size()) == 0
        && std::memcmp(reinterpret_cast<const void*>(focusPublisher), expectedFocusPublisher.data(),
            expectedFocusPublisher.size()) == 0
        && *reinterpret_cast<const std::uint8_t*>(primary) <= 1
        && *reinterpret_cast<const std::uint8_t*>(controller) <= 1;
    if (valid) {
        g_nativePause.setter = reinterpret_cast<SetPrimaryInput>(setter);
        g_nativePause.primary = reinterpret_cast<const std::uint8_t*>(primary);
        g_nativePause.controller = reinterpret_cast<const std::uint8_t*>(controller);
        g_nativePause.focus = reinterpret_cast<const volatile std::uint32_t*>(focus);
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
        valid ? "ev=polled_input stage=native_pause result=ready controller=preserved"
              : "ev=polled_input stage=native_pause result=disabled reason=native_contract");
#endif
}

void resolve_native_key_focus() noexcept {
#if defined(RESTORATION_NATIVE_KEY_FOCUS_OMISSION) && RESTORATION_NATIVE_KEY_FOCUS_OMISSION
    if (g_nativePause.setter == nullptr) return;
    const auto base = g_gameRange.base;
    constexpr std::array<std::uint8_t, 24> focusHead{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x57,0x48,
        0x83,0xEC,0x20,0x8B,0x05,0x0F,0x80,0x2C,0x02,0x41,0x0F,0xB6};
    constexpr std::array<std::uint8_t, 41> enterHead{
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,
        0x3D,0xD7,0x33,0xDD,0x01,0x44,0x8B,0xC1,0x41,0x81,0xE0,0xFF,
        0xFF,0x3F,0x00,0x48,0x81,0xC7,0xA8,0x35,0x00,0x00,0x49,0xC1,
        0xE0,0x06,0x49,0x03,0xF8};
    constexpr std::array<std::uint8_t, 38> leaveHead{
        0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xF9,
        0x8B,0xDA,0x81,0xE7,0xFF,0xFF,0x3F,0x00,0x48,0xC1,0xE7,0x06,
        0x48,0x03,0x3D,0x49,0x33,0xDD,0x01,0xF0,0xFF,0x8F,0xBC,0x35,0x00,0x00};
    constexpr std::array<std::uint8_t, 7> nativeSkipSet{0xC6,0x05,0xC3,0x78,0x2B,0x02,0x01};
    constexpr std::array<std::uint8_t, 7> nativeSkipClear{0x40,0x88,0x35,0xA8,0x78,0x2B,0x02};
    const auto matches = [](std::uintptr_t address, const auto& expected) noexcept {
        return native_pause_range(address, expected.size(), true)
            && std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) == 0;
    };
    bool valid = matches(base + 0x3BE970, focusHead)
        && matches(base + 0x3016D0, enterHead) && matches(base + 0x301750, leaveHead)
        && matches(base + 0x3C5D26, nativeSkipSet) && matches(base + 0x3C5D41, nativeSkipClear)
        && native_pause_range(base + 0x267D5F0, 1, false)
        && native_pause_range(base + 0x26869E0 + 0x19AC, 2, false)
        && native_pause_range(base + 0x26869C8, sizeof(HWND), false)
        && native_pause_range(base + 0x242180C, sizeof(unsigned), false);
    HWND window{};
    DWORD owner{};
    DWORD thread{};
    unsigned queueId{};
    __try {
        if (valid) {
            window = *reinterpret_cast<const HWND*>(base + 0x26869C8);
            thread = GetWindowThreadProcessId(window, &owner);
            valid = window && thread && owner == GetCurrentProcessId()
                && static_cast<std::uintptr_t>(GetClassLongPtrW(window, GCLP_WNDPROC)) == base + 0x3C0400
                && *reinterpret_cast<const std::uint8_t*>(base + 0x267D5F0) <= 1;
            queueId = *reinterpret_cast<const unsigned*>(base + 0x242180C);
            const auto arena = *reinterpret_cast<const std::uintptr_t*>(base + 0x20D4AB8);
            const auto record = arena + 0x35A8 + (queueId & 0x3FFFFFU) * 64;
            const auto dependency = *reinterpret_cast<const unsigned*>(record + 8);
            if (dependency != UINT32_MAX) {
                const auto list = *reinterpret_cast<const std::uintptr_t*>(arena + 0x20);
                const auto rows = *reinterpret_cast<const std::uintptr_t*>(list + 8);
                valid = valid && *reinterpret_cast<const unsigned*>(rows
                    + (dependency & 0xFFFFU) * 0xB8 + 0x70) == 0;
            }
            const auto module = GetModuleHandleW(L"ntdll.dll");
            valid = valid && module
                && *reinterpret_cast<const std::uintptr_t*>(base + 0x3CC7888)
                    == reinterpret_cast<std::uintptr_t>(GetProcAddress(module, "RtlEnterCriticalSection"))
                && *reinterpret_cast<const std::uintptr_t*>(base + 0x3CC7880)
                    == reinterpret_cast<std::uintptr_t>(GetProcAddress(module, "RtlLeaveCriticalSection"));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { valid = false; }
    if (valid) {
        native_key::Bindings bindings{};
        bindings.window = window;
        bindings.windowThread = thread;
        bindings.object = reinterpret_cast<void*>(base + 0x26869E0);
        bindings.skipFocus = reinterpret_cast<volatile unsigned char*>(base + 0x267D5F0);
        bindings.windowFocused = reinterpret_cast<const volatile unsigned char*>(base + 0x26869E0 + 0x19AC);
        bindings.changeSuppressed = reinterpret_cast<const volatile unsigned char*>(base + 0x26869E0 + 0x19AD);
        bindings.primary = g_nativePause.primary;
        bindings.focus = g_nativePause.focus;
        bindings.queueId = queueId;
        bindings.queueEnter = reinterpret_cast<decltype(bindings.queueEnter)>(base + 0x3016D0);
        bindings.queueLeave = reinterpret_cast<decltype(bindings.queueLeave)>(base + 0x301750);
        bindings.setPrimary = g_nativePause.setter;
        bindings.publishFocus = reinterpret_cast<decltype(bindings.publishFocus)>(base + 0x3BE970);
        bindings.foregroundMatches = [](HWND target) { return GetForegroundWindow() == target; };
        bindings.modal = [] { return g_interfaceOpen.load(std::memory_order_acquire)
            && g_nativePauseOwned.load(std::memory_order_acquire); };
        bindings.observeFocus = &observe_native_focus;
        bindings.currentWindowOwner = [](HWND target) {
            const auto image = g_gameRange.base;
            return target == *reinterpret_cast<const HWND*>(image + 0x26869C8)
                && GetWindowThreadProcessId(target, nullptr) == GetCurrentThreadId()
                && static_cast<std::uintptr_t>(GetClassLongPtrW(target, GCLP_WNDPROC))
                    == image + 0x3C0400;
        };
        bindings.realGuardAttached = &native_key_guard_attached;
        bindings.requestRealGuard = &request_native_key_guard;
        valid = g_nativeKeyFocus.configure(bindings);
        if (valid) {
            g_nativePause.keyWindow = window;
            g_nativePause.keyWindowFocused = bindings.windowFocused;
            g_nativePause.keyWindowAddress = reinterpret_cast<const HWND*>(base + 0x26869C8);
        }
    }
    core::log::write(core::log::Channel::client, core::log::Level::info,
        valid ? "ev=polled_input stage=native_key_focus result=ready guard=scoped_native_present"
              : "ev=polled_input stage=native_key_focus result=fallback keyboard=real_guard");
#endif
}

[[nodiscard]] bool caller_is_game(const void* caller) noexcept {
    return diagnostics::contains(g_gameRange, reinterpret_cast<std::uintptr_t>(caller));
}

} // namespace

void observe_native_focus() noexcept {
    if (!g_nativePauseOwned.load(std::memory_order_acquire)) return;
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&g_nativePauseLock);
    refresh_native_focus_locked();
    ReleaseSRWLockExclusive(&g_nativePauseLock);
    SetLastError(error);
}

std::array<hooking::detour::Handle, kHandleCount> g_handles{};
std::array<void*, kHandleCount> g_targets{};

// the scan writes an up-bit from a zero result, so held keys release instead of latching down
__declspec(noinline) SHORT WINAPI get_key_state(int virtualKey) noexcept {
    if (caller_is_game(_ReturnAddress())) {
        // focus gain can enable primary input while a concurrent native scan keeps its sampled
        // enabled bit, so the modal guard must cover that scan
        if (g_interfaceOpen.load(std::memory_order_relaxed)) {
            return kKeyReleased;
        }
    }
    const GetKeyState next = original<GetKeyState>(HookSlot::getKeyState);
    if (next == nullptr) {
        return kKeyReleased;
    }
    return next(virtualKey);
}

__declspec(noinline) SHORT WINAPI get_async_key_state(int virtualKey) noexcept {
    const void* const caller = _ReturnAddress();
    if (g_interfaceOpen.load(std::memory_order_relaxed) && caller_is_game(caller)) {
        return kKeyReleased;
    }
    const GetAsyncKeyState next = original<GetAsyncKeyState>(HookSlot::getAsyncKeyState);
    if (next == nullptr) {
        return kKeyReleased;
    }
    return next(virtualKey);
}

bool resolve_game_range() noexcept {
    const bool found = diagnostics::module_range(GetModuleHandleW(nullptr), g_gameRange);
    if (found) {
        resolve_native_pause();
        resolve_native_key_focus();
    }
    return found;
}

void clear_game_range() noexcept {
    if (!g_nativeKeyFocus.stop()) return;
    apply_native_pause(false);
    AcquireSRWLockExclusive(&g_nativePauseLock);
    g_nativePause = {};
    ReleaseSRWLockExclusive(&g_nativePauseLock);
    g_gameRange = diagnostics::ModuleRange{};
}

void apply_policy(bool visible) noexcept {
    const DWORD error = GetLastError();
    if (!visible) {
        if (g_nativeKeyFocus.configured())
            g_nativeKeyFocus.prepare_close(native_key_recovery_safe());
        apply_native_pause(false);
    }
    if (visible && !g_interfaceOpen.load(std::memory_order_acquire)
        && g_nativeKeyFocus.configured()) {
        // native query draining and pause come before the modal flag the real guard reads
        (void)g_nativeKeyFocus.prepare_open([] { apply_native_pause(true); });
        g_interfaceOpen.store(true, std::memory_order_release);
        SetLastError(error);
        return;
    }
    g_interfaceOpen.store(visible, std::memory_order_relaxed);
    if (visible) apply_native_pause(true);
    SetLastError(error);
}

bool native_pause_owned() noexcept {
    return g_nativePauseOwned.load(std::memory_order_acquire);
}

bool native_pause_available() noexcept {
    AcquireSRWLockShared(&g_nativePauseLock);
    const bool available = g_nativePause.setter != nullptr;
    ReleaseSRWLockShared(&g_nativePauseLock);
    return available;
}

bool drain_native_key_state() noexcept { return g_nativeKeyFocus.stop(); }

bool native_key_focus_available() noexcept { return g_nativeKeyFocus.available(); }

bool native_key_recovery_safe() noexcept {
    if (graphics::native_window::in_preparation()) return false;
    __try {
        const auto image = g_gameRange.base;
        if (!image) return false;
        const auto index = *reinterpret_cast<const unsigned*>(image + 0x330F0E0);
        const auto slots = __readgsqword(0x58);
        if (!slots || index >= 1088) return false;
        const auto tls = *reinterpret_cast<const std::uintptr_t*>(slots + index * sizeof(std::uintptr_t));
        if (!tls || *reinterpret_cast<const std::uintptr_t*>(tls + 0x2558) != 0) return false;
        const auto arena = *reinterpret_cast<const std::uintptr_t*>(image + 0x20D4AB8);
        const auto id = *reinterpret_cast<const unsigned*>(image + 0x242180C);
        const auto record = arena + 0x35A8 + (id & 0x3FFFFFU) * 64;
        const auto owner = *reinterpret_cast<const unsigned*>(record + 0x10);
        const auto depth = *reinterpret_cast<const int*>(record + 0x14);
        const auto current = *reinterpret_cast<const unsigned*>(tls + 0x88);
        return depth <= 0 || owner != current;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

LRESULT forward_with_native_focus(WNDPROC original, HWND window, UINT message,
                                 WPARAM word, LPARAM value) noexcept {
    const DWORD entryError = GetLastError();
    const bool nativeCommand = message >= WM_USER && message <= WM_USER + 15;
    const bool safePublication = !nativeCommand && message != WM_CAPTURECHANGED
        && native_key_recovery_safe();
    auto token = g_nativeKeyFocus.begin(window, message, safePublication);
    SetLastError(entryError);
    LRESULT result{};
    __try {
        result = original ? CallWindowProcW(original, window, message, word, value)
                          : DefWindowProcW(window, message, word, value);
    } __finally { g_nativeKeyFocus.end(token); }
    const DWORD resultError = GetLastError();
    // native 40D has finished and released its owner before deferred publication
    const bool afterFrame = message == WM_USER + 13 && native_key_recovery_safe();
    g_nativeKeyFocus.service_deferred(safePublication || afterFrame);
    observe_native_focus();
    SetLastError(resultError);
    return result;
}

void apply_visibility(bool visible) noexcept {
    apply_policy(visible);
}

} // namespace dawn::client::hooks::polled_input
