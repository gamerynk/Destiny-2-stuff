#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

#include "../dawn/src/client/hooks/polled_input/polled_input_replacements.cpp"
#include "../dawn/src/client/hooks/graphics/input/graphics_window_input.cpp"

namespace f {
namespace p = dawn::client::hooks::polled_input;
namespace input = dawn::client::hooks::graphics::input;
std::atomic_uint checks{}, forwards{}, requests{}, frames{};
std::atomic_bool guardAttached{};
thread_local bool preparation{};
volatile unsigned char skip{}, focused{1}, suppressed{};
std::uint8_t primary{1}, controller{1};
volatile unsigned focus{1};
HWND currentWindow{};
CRITICAL_SECTION queue;
unsigned queueDepth{}, publications{};
constexpr UINT testMessage = WM_APP + 101;
constexpr DWORD entryError = 1234, originalError = 5678;
constexpr LRESULT result = 0x5A5A;
void require(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
void enter(unsigned, int) { EnterCriticalSection(&queue); ++queueDepth; }
void leave(unsigned, int) { --queueDepth; LeaveCriticalSection(&queue); }
void setPrimary(bool enabled) { primary = enabled ? 1 : 0; SetLastError(9001); }
void publish(void*, HWND, bool) { ++publications; }
bool foreground(HWND) { return true; }
bool modal() { return p::g_interfaceOpen.load() && p::native_pause_owned(); }
bool currentOwner(HWND target) {
    return target == currentWindow && GetWindowThreadProcessId(target, nullptr) == GetCurrentThreadId();
}
LRESULT CALLBACK original(HWND window, UINT message, WPARAM word, LPARAM value) {
    if (message == testMessage) {
        ++forwards;
        require(GetLastError() == entryError, "production helper preserves original entry LastError");
        require(skip == (guardAttached.load() ? 0 : 1), "owned original receives scoped or recovered guard state");
        if (value == 1) {
            preparation = true;
            SendMessageW(window, testMessage, word, 0);
            preparation = false;
            require(skip == 1, "preparation reentry preserves the outer native skip guard");
        }
        SetLastError(originalError);
        return result;
    }
    if (message >= WM_USER && message <= WM_USER + 15) {
        ++forwards;
        require(skip == 0, "actual owned subclass preserves all native command-range skip behavior");
        SetLastError(originalError);
        return result;
    }
    return DefWindowProcW(window, message, word, value);
}
HWND makeWindow() {
    const auto window = CreateWindowExW(0, L"NativeKeyFocusFixture", L"hidden", 0,
        0, 0, 32, 32, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window != nullptr && !IsWindowVisible(window), "private hidden output HWND created");
    return window;
}
void configure(HWND window) {
    p::native_key::Bindings bindings{window, GetCurrentThreadId(), nullptr, &skip, &focused,
        &suppressed, &primary, &focus, 28, &enter, &leave, &setPrimary, &publish,
        &foreground, &modal, &p::observe_native_focus, &currentOwner,
        [] { return guardAttached.load(); }, &p::request_native_key_guard};
    require(p::g_nativeKeyFocus.configure(bindings), "actual production native policy accepts private bindings");
    p::g_nativePause.setter = &setPrimary;
    p::g_nativePause.primary = &primary;
    p::g_nativePause.controller = &controller;
    p::g_nativePause.focus = &focus;
}
}

namespace dawn::core::log {
void write(Channel, Level, std::string_view) noexcept { SetLastError(9002); }
}
namespace dawn::client::diagnostics {
bool module_range(HMODULE, ModuleRange&) noexcept { return false; }
bool contains(const ModuleRange& range, std::uintptr_t address) noexcept {
    return address >= range.base && address < range.end;
}
}
namespace dawn::client::hooks::polled_input {
void request_native_key_guard() noexcept { ++f::requests; SetLastError(9003); }
bool native_key_guard_attached() noexcept { return f::guardAttached.load(); }
}
namespace dawn::core::ui::runtime {
VisibilitySnapshot snapshot() noexcept { return {true, true, true, VK_INSERT}; }
bool toggle_for_key(UINT) noexcept { return false; }
}
namespace dawn::core::ui::modules::logs {
void dispatch_pending_copy(HWND) noexcept {}
}
namespace dawn::client::hooks::graphics::renderer {
bool handle_window_message(HWND, UINT message, WPARAM, LPARAM) noexcept {
    if (message == f::testMessage) SetLastError(f::entryError);
    return false;
}
bool handle_preparation_message(HWND, UINT message, WPARAM, LPARAM) noexcept {
    return handle_window_message(nullptr, message, 0, 0);
}
void dispatch_pending_input_release(HWND) noexcept {}
}
namespace dawn::client::hooks::graphics::native_window {
bool in_preparation() noexcept { return f::preparation; }
FrameScope::FrameScope(HWND) noexcept { ++f::frames; }
FrameScope::~FrameScope() noexcept = default;
}

int main() {
    using namespace f;
    InitializeCriticalSection(&queue);
    WNDCLASSW wc{}; wc.lpfnWndProc = &original; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"NativeKeyFocusFixture";
    require(RegisterClassW(&wc) != 0, "hidden class registered");
    currentWindow = makeWindow();
    configure(currentWindow);
    require(input::install(currentWindow), "actual main output subclass installs");
    SetLastError(entryError);
    p::apply_policy(true);
    require(GetLastError() == entryError && p::g_interfaceOpen.load() && primary == 0
        && controller == 1 && queueDepth == 0, "production opening drains/pause before modal publish and preserves controller/LastError");
    SetLastError(entryError);
    require(p::forward_with_native_focus(&original, currentWindow, testMessage, 0, 0) == result
        && GetLastError() == originalError && skip == 0,
        "actual production forward preserves original result and exit LastError");
    require(SendMessageW(currentWindow, testMessage, 0, 1) == result && skip == 0,
        "actual subclass supports synchronous renderer-preparation reentry");
    for (UINT message = WM_USER; message <= WM_USER + 15; ++message)
        require(SendMessageW(currentWindow, message, 0, 0) == result, "native command forwarded unchanged");
    require(frames == 1 && publications == 0 && requests == 0,
        "only40D enters presentation helper; no publisher runs in fixture unsafe graphics scope");
    focus = 0;
    p::apply_policy(false);
    require(publications == 0 && !p::g_interfaceOpen.load(),
        "actual close cannot publish genuine focus when ownership safety is unavailable");
    focus = 1; primary = 1; p::apply_policy(true);
    require(input::uninstall(), "old actual output subclass drains before window replacement");
    DestroyWindow(currentWindow);
    std::thread newOwner([] {
        currentWindow = makeWindow();
        require(input::install(currentWindow), "new HWND on another real owner thread accepts subclass");
        const auto before = forwards.load();
        require(SendMessageW(currentWindow, testMessage, 0, 0) == result && forwards == before + 1,
            "authoritative replacement HWND original message is never discarded during recovery request");
        require(requests != 0 && p::g_nativeKeyFocus.fallback_requested() && skip == 0,
            "thread/ HWND migration requests worker recovery and restores transient guard");
        guardAttached.store(true);
        require(SendMessageW(currentWindow, testMessage, 0, 0) == result,
            "attached real recovery guard permits new native owner original procedure");
        require(input::uninstall(), "new owner subclass retires cleanly");
        DestroyWindow(currentWindow);
    });
    newOwner.join();
    p::apply_policy(false);
    require(!p::g_interfaceOpen.load() && primary == 1 && controller == 1,
        "production close restores saved foreground primary and leaves controller policy unchanged");
    require(p::drain_native_key_state(), "actual key metadata retires after subclass drain");
    primary = 0; p::apply_policy(true); p::apply_policy(false);
    require(primary == 0, "originally disabled primary remains disabled after ordinary open/close");
    primary = 1; p::apply_policy(true);
    currentWindow = makeWindow();
    p::g_nativePause.keyWindowAddress = &currentWindow;
    p::g_nativePause.keyWindowFocused = &focused;
    p::apply_policy(false);
    require(primary == 0 && controller == 1,
        "actual hidden/background HWND close never enables gameplay input");
    DestroyWindow(currentWindow);
    p::clear_game_range();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    DeleteCriticalSection(&queue);
    std::printf("PASS %u actual source visibility/LastError/hidden-owner migration/reentry checks; no game writes\n", checks.load());
}
