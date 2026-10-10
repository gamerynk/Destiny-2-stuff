#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "../dawn/src/client/hooks/graphics/graphics_hook_replacements.cpp"
LRESULT WINAPI boundary_default_window_proc(HWND, UINT, WPARAM, LPARAM);
LRESULT WINAPI boundary_forward_window_proc(WNDPROC, HWND, UINT, WPARAM, LPARAM);
#define DefWindowProcW boundary_default_window_proc
#define CallWindowProcW boundary_forward_window_proc
#include "../dawn/src/client/hooks/graphics/input/graphics_window_input.cpp"
#include "../dawn/src/client/hooks/graphics/input/graphics_raw_input_window.cpp"
#undef DefWindowProcW
#undef CallWindowProcW
#include "../dawn/src/client/hooks/teleport/teleport_lifecycle.cpp"
#include "../dawn/src/client/hooks/cursor/cursor_guard_replacements.cpp"
#include "../dawn/src/client/hooks/polled_input/polled_input_replacements.cpp"
#include "../dawn/src/client/hooks/bitmap/bitmap_ref_guard.cpp"
#include "../dawn/src/client/hooks/queuez/svc123_null_payload_guard.cpp"
#include "../dawn/src/client/hooks/queuez/queuez_hook_lifecycle.cpp"
#include "../dawn/src/client/hooks/retail_log/retail_log_lifecycle.cpp"
#include "../dawn/src/client/hooks/assert_handler/assert_handler_observer.cpp"
#include "../dawn/src/client/hooks/assert_handler/assert_handler_lifecycle.h"

namespace fixture {
int checks{};
std::vector<std::string> events;
dawn::core::ui::runtime::VisibilitySnapshot visibility;
bool debugLogs{};
bool gameCaller{true};
std::uint8_t nativePrimary{}, nativeController{};
unsigned nativePauseCalls{};
std::uint32_t nativeFocus{};
bool nativeFocusWindow{};
bool nativeFocusError{};
bool failDetach{};
unsigned clearPlayerTargets{};
void __fastcall native_primary(bool enabled) {
    nativePrimary = enabled ? 1 : 0;
    ++nativePauseCalls;
    if (nativeFocusError) SetLastError(ERROR_ACCESS_DENIED);
}
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void event(const char* value) { events.emplace_back(value); }
}

namespace dawn::core::log {
bool accepts(Channel, Level level) noexcept { return level != Level::debug || fixture::debugLogs; }
void write(Channel, Level, std::string_view text) noexcept {
    fixture::events.emplace_back(text);
}
}
namespace dawn::client::patterns {
std::byte* scan_main_image_unique(std::span<const PatternByte>, const char*) noexcept { return nullptr; }
std::byte* resolve_relative(const std::byte*, const std::byte*) noexcept { return nullptr; }
}
namespace dawn::client::hooking::detour {
bool install(const Spec&, Handle&) noexcept { fixture::require(false, "test must never attach detour"); return false; }
bool install(std::span<const Spec>, std::span<Handle>) noexcept { fixture::require(false, "test must never attach detours"); return false; }
bool uninstall(Handle&) noexcept { return true; }
bool uninstall(std::span<Handle>) noexcept { return !fixture::failDetach; }
UninstallResult uninstall(Handle&, std::span<const ProtectedCodeEntry>, IdleCheck idle) noexcept {
    if (idle != nullptr && !idle()) return UninstallResult::protectedCodeActive;
    return fixture::failDetach ? UninstallResult::failed : UninstallResult::removed;
}
}
namespace dawn::client::diagnostics {
bool contains(const ModuleRange&, std::uintptr_t) noexcept { return fixture::gameCaller; }
bool module_range(HMODULE, ModuleRange&) noexcept { return false; }
}
namespace dawn::client::input { bool game_focused() noexcept { return false; } }
namespace dawn::core::ui::runtime {
VisibilitySnapshot snapshot() noexcept { return fixture::visibility; }
bool toggle_for_key(UINT key) noexcept {
    if (key != fixture::visibility.toggleVirtualKey) return false;
    fixture::visibility.visible = !fixture::visibility.visible;
    return true;
}
}
namespace dawn::core::ui::modules::logs { void dispatch_pending_copy(HWND) noexcept { fixture::event("copy_dispatch"); } }
namespace dawn::client::hooks::teleport {
void publish_targets(ControlledHandle) noexcept {}
void publish_native_player_lookup(NativePlayerBodyLookup) noexcept {}
void clear_targets() noexcept { ++fixture::clearPlayerTargets; }
}
namespace dawn::client::player::position {
void poll() noexcept { fixture::event("position_poll"); }
void observe(void*) noexcept { fixture::event("position_observe"); }
void reset() noexcept {}
}
namespace dawn::client::hooks::bootflow { void poll_world_step() noexcept { fixture::event("world_poll"); } }
namespace dawn::client::hooks::graphics {
SRWLOCK g_hookLock{SRWLOCK_INIT};
std::atomic_uint g_activeHookCalls{};
thread_local std::size_t g_hookCallDepth{};
bool g_acceptRendererCalls{true};
std::array<hooking::detour::Handle, kHandleCount> g_handles{};
std::array<void*, kHandleCount> g_targetEntries{};
Targets g_targets{};
namespace renderer {
void present(IDXGISwapChain*, ID3D11DeviceContext*) noexcept { fixture::event("overlay_present"); }
void present_result(IDXGISwapChain*, HRESULT) noexcept { fixture::event("present_result"); }
void before_surface_change(IDXGISwapChain*) noexcept { fixture::event("release_backbuffer"); }
void after_surface_change(IDXGISwapChain*, HRESULT) noexcept { fixture::event("rebuild_backbuffer"); }
void dispatch_pending_input_release() noexcept {
    fixture::require(g_activeHookCalls.load() == 0 && g_hookCallDepth == 0, "capture release runs outside hook lifetime lock");
    fixture::event("capture_release");
}
void dispatch_pending_input_release(HWND) noexcept { fixture::event("window_release"); }
bool handle_preparation_message(HWND window, UINT message, WPARAM word, LPARAM value) noexcept {
    return handle_window_message(window, message, word, value);
}
bool handle_window_message(HWND, UINT message, WPARAM, LPARAM) noexcept {
    fixture::event("imgui_message");
    return fixture::visibility.visible && (message == WM_INPUT || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_MOUSEWHEEL);
}
}
namespace native_window {
bool in_preparation() noexcept { return false; }
FrameScope::FrameScope(HWND) noexcept {}
FrameScope::~FrameScope() noexcept {}
}
}

LRESULT WINAPI boundary_default_window_proc(HWND, UINT, WPARAM, LPARAM) { fixture::event("default_proc"); return 81; }
LRESULT WINAPI boundary_forward_window_proc(WNDPROC proc, HWND window, UINT message, WPARAM word, LPARAM value) {
    return proc(window, message, word, value);
}
namespace dawn::client::targets::game::assert_handler {
Targets fixtureTargets;
const Targets& get() noexcept { return fixtureTargets; }
bool is_resolved() noexcept { return fixtureTargets.slot != nullptr; }
}

namespace {
LRESULT CALLBACK native_window(HWND, UINT message, WPARAM word, LPARAM) {
    fixture::event("game_window");
    if (fixture::nativeFocusWindow && message == WM_ACTIVATE) {
        fixture::nativeFocus = LOWORD(word) != WA_INACTIVE ? 1 : 0;
        fixture::native_primary(fixture::nativeFocus != 0);
        if (fixture::nativeFocusError) SetLastError(ERROR_BUSY);
    }
    return 97;
}
HRESULT STDMETHODCALLTYPE native_present(IDXGISwapChain*, UINT sync, UINT flags) {
    fixture::require(sync == 2, "Present sync forwarded");
    fixture::event(flags & DXGI_PRESENT_TEST ? "native_present_test" : "native_present");
    return S_FALSE;
}
HRESULT STDMETHODCALLTYPE native_resize(IDXGISwapChain*, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags) {
    fixture::require(count == 3 && w == 640 && h == 480 && format == DXGI_FORMAT_R8G8B8A8_UNORM && flags == 7, "ResizeBuffers arguments forwarded");
    fixture::event("native_resize"); return DXGI_ERROR_INVALID_CALL;
}
BOOL WINAPI native_cursor(int x, int y) { fixture::require(x == 12 && y == 34, "cursor coordinates forwarded"); fixture::event("cursor_move"); return FALSE; }
RECT lastClip{};
BOOL WINAPI native_clip(const RECT* bounds) { if (bounds) lastClip = *bounds; fixture::event(bounds ? "clip" : "unclip"); return TRUE; }
SHORT WINAPI native_key(int) { fixture::event("real_key"); return static_cast<SHORT>(0x8001); }
std::uint32_t bitmapTriple[3]{};
bool bitmapNull{};
void __fastcall native_bitmap(void*, void* ref) { bitmapNull = ref == nullptr; if (ref) std::memcpy(bitmapTriple, ref, sizeof bitmapTriple); }
char __fastcall native_queuez(void*, void*, std::byte*) { fixture::event("queuez_native"); return 7; }
std::byte* assertSlot{};
void __fastcall set_assert(void* handler) { assertSlot = static_cast<std::byte*>(handler); }
void __cdecl native_assert(int code, const char*, ...) { fixture::require(code == 6, "only device-loss assert remains fatal"); fixture::event("fatal_native"); }
void graphics_test() {
    using namespace dawn::client::hooks::graphics;
    const auto entries = replacement::entry_points();
    g_handles[0].original = reinterpret_cast<void*>(&native_present);
    g_handles[1].original = reinterpret_cast<void*>(&native_resize);
    fixture::events.clear();
    fixture::require(replacement::present(nullptr, 2, 0) == S_FALSE, "Present preserves native HRESULT");
    fixture::require(fixture::events == std::vector<std::string>{"overlay_present", "native_present", "present_result", "capture_release"}, "overlay/native/result/release order");
    fixture::events.clear();
    (void)replacement::present(nullptr, 2, DXGI_PRESENT_TEST);
    fixture::require(fixture::events.front() == "native_present_test", "DXGI TEST does not draw overlay");
    fixture::events.clear();
    fixture::require(reinterpret_cast<ResizeBuffers>(entries[1])(nullptr, 3, 640, 480, DXGI_FORMAT_R8G8B8A8_UNORM, 7) == DXGI_ERROR_INVALID_CALL, "resize failure preserved");
    fixture::require(fixture::events == std::vector<std::string>{"release_backbuffer", "native_resize", "rebuild_backbuffer"}, "resize releases RTV before native surface change");
    fixture::require(entries.size() == 2 && replacement::protected_entries().size() == 4, "graphics lifecycle owns only Present and ResizeBuffers");
}
void input_test() {
    namespace c = dawn::client::hooks::cursor;
    c::g_targets[0] = reinterpret_cast<void*>(&native_cursor); c::g_targets[1] = reinterpret_cast<void*>(&native_clip);
    fixture::events.clear();
    fixture::require(c::set_cursor_pos(12, 34) == FALSE, "hidden cursor preserves native failure");
    RECT first{1, 2, 20, 30}; (void)c::clip_cursor(&first);
    c::apply_visibility(true); fixture::events.clear();
    fixture::require(c::set_cursor_pos(77, 88) == TRUE && fixture::events.empty(), "open overlay blocks cursor recenter");
    RECT updated{3, 4, 50, 60}; (void)c::clip_cursor(&updated);
    fixture::require(fixture::events.back() == "unclip", "open overlay defeats native clip request");
    c::apply_visibility(false);
    fixture::require(lastClip.left == 3 && lastClip.bottom == 60, "close restores latest native confinement");
    namespace p = dawn::client::hooks::polled_input;
    p::g_targets[0] = reinterpret_cast<void*>(&native_key); p::g_targets[1] = reinterpret_cast<void*>(&native_key);
    fixture::require(p::get_key_state(VK_SHIFT) == static_cast<SHORT>(0x8001), "hidden game key preserves toggle/held bits");
    fixture::gameCaller = false;
    fixture::require(p::get_key_state('W') == static_cast<SHORT>(0x8001), "UI key read preserves actual state");
    p::apply_visibility(true); fixture::require(p::get_async_key_state('W') == static_cast<SHORT>(0x8001), "overlay retains actual async keyboard state");
    fixture::gameCaller = true;
    fixture::require(p::get_key_state('W') == 0 && p::get_async_key_state('W') == 0, "visible overlay releases both game polling APIs without forced input");
    p::g_nativePauseOwned.store(true);
    fixture::require(p::get_key_state('W') == 0,
        "native pause ownership cannot bypass modal release during native focus reassertion");
    p::g_nativePauseOwned.store(false);
    SetLastError(ERROR_BUSY);
    fixture::require(p::get_async_key_state('W') == 0 && GetLastError() == ERROR_BUSY,
        "modal async guard retains the released result and caller error");
    p::apply_visibility(false);
    for (const std::uint8_t primary : {std::uint8_t{0}, std::uint8_t{1}}) {
        for (const std::uint8_t controller : {std::uint8_t{0}, std::uint8_t{1}}) {
            fixture::nativePrimary = primary; fixture::nativeController = controller;
            fixture::nativePauseCalls = 0;
            p::g_nativePause = {&fixture::native_primary, &fixture::nativePrimary,
                &fixture::nativeController, 0, false};
            p::apply_policy(true); p::apply_policy(true);
            fixture::require(fixture::nativePrimary == 0 && fixture::nativeController == controller
                && fixture::nativePauseCalls == 1,
                "native route pauses primary once and preserves either controller policy");
            fixture::require(p::get_key_state(VK_SHIFT) == 0
                && p::get_async_key_state(VK_SHIFT) == 0,
                "native pause retains both modal guards across native focus reassertion");
            p::clear_game_range();
            fixture::require(fixture::nativePrimary == primary && fixture::nativeController == controller
                && fixture::nativePauseCalls == 2 && !p::g_nativePauseOwned.load(),
                "native route shutdown restores exact prior primary and leaves controller untouched");
            p::apply_policy(false);
        }
    }
    namespace i = dawn::client::hooks::graphics::input;
    const HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1));
    i::g_binding = {window, &native_window, true};
    fixture::nativeFocusWindow = true;
    fixture::nativeController = 0;
    fixture::nativePrimary = 1; fixture::nativeFocus = 1;
    p::g_nativePause = {&fixture::native_primary, &fixture::nativePrimary,
        &fixture::nativeController, 0, false, &fixture::nativeFocus, 0};
    p::apply_policy(true);
    fixture::require(i::window_procedure(window, WM_ACTIVATE, WA_INACTIVE, 0) == 97
        && fixture::nativePrimary == 0, "native focus loss is forwarded while interface pause is owned");
    p::apply_policy(false);
    fixture::require(fixture::nativePrimary == 0,
        "close after focus loss restores native unfocused state instead of stale pre-open enabled bit");
    p::apply_policy(true);
    fixture::nativeFocusError = true;
    fixture::require(i::window_procedure(window, WM_ACTIVATE, WA_ACTIVE, 0) == 97
        && fixture::nativePrimary == 0 && GetLastError() == ERROR_BUSY,
        "native focus gain is forwarded then primary pause reapplied preserving native result and error");
    fixture::nativeFocusError = false;
    p::apply_policy(false);
    fixture::require(fixture::nativePrimary == 1 && fixture::nativeController == 0,
        "close after native focus gain restores newly published enabled state with controller preserved");
    fixture::nativePrimary = 0; fixture::nativeFocus = 1;
    p::apply_policy(true);
    (void)i::window_procedure(window, WM_ACTIVATE, WA_INACTIVE, 0);
    (void)i::window_procedure(window, WM_ACTIVATE, WA_ACTIVE, 0);
    p::apply_policy(false);
    fixture::require(fixture::nativePrimary == 1,
        "lost-regained focus supersedes an earlier independently paused baseline even when final focus matches initial");
    p::apply_policy(true);
    fixture::nativeFocus = 0; fixture::nativePrimary = 0;
    p::clear_game_range();
    fixture::require(fixture::nativePrimary == 0 && !p::g_nativePauseOwned.load(),
        "shutdown refreshes current native focus even without a window observer callback");
    p::apply_policy(false);
    fixture::nativeFocusWindow = false;
}
void window_input_test() {
    namespace i = dawn::client::hooks::graphics::input;
    const HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1));
    i::g_binding = {window, &native_window, true};
    i::g_rawBinding = {window, &native_window, true};
    fixture::visibility.visible = true; fixture::events.clear();
    fixture::require(i::window_procedure(window, i::kRequiredForwardMessage, 0, 0) == 97,
                     "native presentation message forwarded while overlay visible");
    fixture::require(fixture::events == std::vector<std::string>{"game_window", "window_release", "copy_dispatch"},
                     "presentation message finishes native call before deferred release/copy");
    fixture::events.clear();
    fixture::require(i::window_procedure(window, WM_CAPTURECHANGED, 0, 0) == 97 && fixture::events == std::vector<std::string>{"game_window"},
                     "capture reentrancy forwards without entering renderer");
    fixture::events.clear();
    fixture::require(i::window_procedure(window, WM_KEYDOWN, 'W', 0) == 1 && fixture::events == std::vector<std::string>{"imgui_message", "window_release"},
                     "visible overlay key message never reaches game");
    fixture::events.clear();
    fixture::require(i::raw_window_procedure(window, WM_INPUT, 0, 0) == 81 && fixture::events == std::vector<std::string>{"default_proc"},
                     "captured hidden-sink raw mouse delivery cleaned by default procedure");
#if defined(RESTORATION_NATIVE_RAW_MOUSE_PAUSE) && RESTORATION_NATIVE_RAW_MOUSE_PAUSE
    namespace p = dawn::client::hooks::polled_input;
    fixture::nativePrimary = 1;
    p::g_nativePause = {&fixture::native_primary, &fixture::nativePrimary,
        &fixture::nativeController, 0, false};
    p::apply_policy(true); fixture::events.clear();
    fixture::require(i::raw_window_procedure(window, WM_INPUT, 0, 0) == 97
        && fixture::events == std::vector<std::string>{"game_window"},
        "validated native pause receives original raw input without subclass suppression");
    p::clear_game_range(); fixture::events.clear();
    fixture::require(i::raw_window_procedure(window, WM_INPUT, 0, 0) == 81
        && fixture::events == std::vector<std::string>{"default_proc"},
        "failed or cleared native pause retains raw mouse capture fallback");
    p::apply_policy(false);
#endif
    fixture::visibility.visible = false; fixture::events.clear();
    fixture::require(i::raw_window_procedure(window, WM_INPUT, 0, 0) == 97 && fixture::events == std::vector<std::string>{"game_window"},
                     "hidden overlay raw mouse delivery remains native");
    fixture::events.clear();
    fixture::require(i::window_procedure(window, WM_KEYDOWN, VK_INSERT, 0) == 1,
                     "toggle key down consumed even while overlay hidden");
    fixture::require(i::window_procedure(window, WM_KEYUP, VK_INSERT, 0) == 1 && fixture::visibility.visible,
                     "toggle key up changes visibility and remains consumed");
    fixture::require(i::g_activeCallbacks.load() == 0 && i::g_rawActiveCallbacks.load() == 0,
                     "ordinary and raw-sink callbacks release teardown leases");
    fixture::visibility.visible = false;
}
void movement_phase_test() {
    namespace t = dawn::client::hooks::teleport;
    fixture::require(t::install(), "missing optional observer target does not block startup");
    const auto priorTargets = fixture::clearPlayerTargets;
    fixture::require(t::uninstall() && !t::g_installed.load()
        && fixture::clearPlayerTargets == priorTargets + 1,
        "native observer shutdown clears helpers without a movement detour");
}
void guards_test() {
    namespace b = dawn::client::hooks::bitmap;
    b::g_original.store(&native_bitmap);
    std::uint32_t bad[3]{77, 0x12345678, 3};
    b::setter(nullptr, bad);
    fixture::require(bitmapTriple[b::BitmapRefWords::reference] == b::kNoneReference && bad[1] == 0x12345678 && bitmapTriple[0] == 77 && bitmapTriple[2] == 3, "invalid bitmap reference replaced in copy only");
    bad[1] = 0x80A146C7; b::setter(nullptr, bad); fixture::require(bitmapTriple[1] == bad[1], "authored bitmap reference preserved");
    b::setter(nullptr, nullptr); fixture::require(bitmapNull, "null bitmap descriptor forwarded");
    namespace q = dawn::client::hooks::queuez;
    q::g_original.store(&native_queuez); q::g_processedUpdateGeneration.store(0);
    std::array<std::byte, 16> message{}; fixture::events.clear();
    std::array<std::uint32_t, 4> responseContext{0x80801234U};
    q::g_reported.store(false);
    SetLastError(ERROR_BUSY);
    fixture::require(q::handler(nullptr, responseContext.data(), message.data()) == 0
        && q::processed_update_generation() == 0 && GetLastError() == ERROR_BUSY,
        "null QueueZ publication does not call native, advance generation or alter last error");
    std::uintptr_t payload = 1; std::memcpy(message.data()+8, &payload, sizeof payload);
    fixture::require(q::handler(nullptr, nullptr, message.data()) == 7 && q::processed_update_generation() == 1, "valid QueueZ completion retains native result and advances generation");
    namespace r = dawn::client::hooks::retail_log;
    fixture::require(r::install() && !r::is_installed() && r::uninstall(),
        "native logging lifecycle remains passive and installs no detour");
    namespace a = dawn::client::hooks::assert_handler;
    auto& targets = dawn::client::targets::game::assert_handler::fixtureTargets;
    assertSlot = reinterpret_cast<std::byte*>(&native_assert); targets = {&assertSlot, reinterpret_cast<void*>(&native_assert), &set_assert};
    fixture::require(a::install() && a::is_installed(), "assert uses native callback setter");
    auto callback = reinterpret_cast<void(__cdecl*)(int, const char*, ...)>(assertSlot);
    fixture::events.clear(); callback(1, "recoverable %d", 12);
    fixture::require(fixture::events.size() == 1 && fixture::events[0].find("recoverable 12") != std::string::npos, "recoverable assert logs formatted message and returns");
    fixture::events.clear(); callback(6, "lost device");
    fixture::require(fixture::events.back() == "fatal_native", "device-loss assert preserves native fatal path");
    fixture::require(a::uninstall() && assertSlot == reinterpret_cast<std::byte*>(&native_assert), "assert uninstall restores exact original callback");
}

}
int main() {
    graphics_test(); input_test(); window_input_test(); movement_phase_test(); guards_test();
    std::printf("PASS %d actual UI/movement hook boundary checks; no game writes or attachments\n", fixture::checks);
}
