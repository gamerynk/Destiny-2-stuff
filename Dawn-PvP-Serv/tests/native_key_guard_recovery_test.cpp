#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../dawn/src/client/hooks/polled_input/polled_input_lifecycle.cpp"

static_assert(RESTORATION_NATIVE_ASYNC_OMISSION == 1,
              "This fixture covers the enabled native key + Async omission path.");

namespace fixture {
unsigned checks{}, installCalls{}, uninstallCalls{}, clears{}, pauseCloses{};
bool keyReady{true}, pauseReady{true}, safe{}, drain{true};
bool failAttach{}, failDetach{};
std::string lastLog;
void require(bool ok, const char* why) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
}

namespace dawn::core::log {
void write(Channel, Level, std::string_view line) noexcept { fixture::lastLog = line; }
}

namespace dawn::client::hooks::polled_input {
std::array<hooking::detour::Handle, kHandleCount> g_handles{};
std::array<void*, kHandleCount> g_targets{};
bool native_pause_available() noexcept { return fixture::pauseReady; }
bool native_key_focus_available() noexcept { return fixture::keyReady; }
bool native_key_recovery_safe() noexcept { return fixture::safe; }
bool resolve_game_range() noexcept { return true; }
void clear_game_range() noexcept { ++fixture::clears; }
bool drain_native_key_state() noexcept { return fixture::drain; }
void apply_policy(bool visible) noexcept {
    fixture::require(!visible, "teardown only closes the modal policy");
    ++fixture::pauseCloses;
}
SHORT WINAPI get_key_state(int) noexcept { return 0; }
SHORT WINAPI get_async_key_state(int) noexcept { return 0; }
}

namespace dawn::client::hooking::detour {
bool install(std::span<const Spec> specs, std::span<Handle> outputs) noexcept {
    namespace p = dawn::client::hooks::polled_input;
    ++fixture::installCalls;
    fixture::require(specs.size() == 1 && outputs.size() == 1,
                     "recovery attaches exactly one synchronous slot");
    fixture::require(outputs.data() == p::g_handles.data(),
                     "recovery publishes the original key slot, not the Async slot");
    fixture::require(specs[0].target == p::g_targets[0]
        && specs[0].replacement == reinterpret_cast<void*>(&p::get_key_state),
        "recovery uses the resolved real key export and actual replacement ABI");
    if (fixture::failAttach) return false;
    outputs[0] = {specs[0].target, specs[0].replacement, true};
    return true;
}
bool uninstall(std::span<Handle> handles) noexcept {
    namespace p = dawn::client::hooks::polled_input;
    ++fixture::uninstallCalls;
    fixture::require(handles.data() == p::g_handles.data() && handles.size() == p::kHandleCount,
                     "teardown covers both stable slot identities");
    if (fixture::failDetach) return false;
    for (auto& handle : handles) handle = {};
    return true;
}
}

int main() {
    namespace p = dawn::client::hooks::polled_input;
    using fixture::require;
    require(p::install() && p::is_installed(), "enabled native contracts install successfully");
    require(fixture::installCalls == 0 && !p::g_handles[0].attached && !p::g_handles[1].attached,
            "initial enabled path physically attaches zero detours");
    require(p::g_targets[0] != nullptr && p::g_targets[1] != nullptr,
            "both omitted real exports stay resolved for later recovery");
    require(!p::native_key_guard_attached(), "zero-hook startup reports no key guard");
    require(fixture::lastLog.find("hooks=0 keyboard=native_focus_trial") != std::string::npos,
            "startup diagnostic reports actual zero-handle selection");
    void* const realKey = p::g_targets[0];
    void* const realAsync = p::g_targets[1];
    require(p::install() && fixture::installCalls == 0, "repeat install preserves zero-hook selection");

    fixture::safe = true;
    p::service_native_key_recovery();
    require(fixture::installCalls == 0, "safe service without a request does not attach a guard");
    fixture::safe = false;
    p::request_native_key_guard();
    p::request_native_key_guard();
    p::service_native_key_recovery();
    require(fixture::installCalls == 0 && !p::native_key_guard_attached(),
            "unsafe ownership defers requested recovery without a transaction");

    fixture::safe = true;
    fixture::failAttach = true;
    p::service_native_key_recovery();
    require(fixture::installCalls == 1 && !p::g_handles[0].attached
        && !p::native_key_guard_attached(), "failed attachment never reports a live guard");
    require(p::g_targets[0] == realKey && p::g_targets[1] == realAsync && p::is_installed(),
            "failed recovery retains resolved exports and selected native lifecycle");
    require(fixture::lastLog.find("fallback_pending") != std::string::npos,
            "attachment failure reports pending recovery");
    fixture::safe = false;
    fixture::failAttach = false;
    p::service_native_key_recovery();
    require(fixture::installCalls == 1, "pending recovery still respects unsafe ownership");
    fixture::safe = true;
    p::service_native_key_recovery();
    require(fixture::installCalls == 2 && p::native_key_guard_attached(),
            "retained request retries and reports the successful zero-to-one transition");
    require(p::g_handles[0].attached && p::g_handles[0].original == realKey
        && !p::g_handles[1].attached && p::g_targets[1] == realAsync,
        "only the synchronous guard changes; Async remains physically omitted");
    require(fixture::lastLog.find("fallback_attached hooks=1") != std::string::npos,
            "successful recovery reports one actual attached guard");
    p::service_native_key_recovery();
    p::request_native_key_guard();
    p::service_native_key_recovery();
    require(fixture::installCalls == 2, "completed and duplicate requests do not reattach a live slot");

    fixture::drain = false;
    p::uninstall();
    require(fixture::uninstallCalls == 0 && fixture::pauseCloses == 0 && fixture::clears == 0,
            "admitted native ownership prevents teardown before policy close or detach");
    require(p::is_installed() && p::native_key_guard_attached() && p::g_targets[0] == realKey,
            "busy native ownership preserves guard, exports and installed state for retry");
    fixture::drain = true;
    fixture::failDetach = true;
    p::uninstall();
    require(fixture::uninstallCalls == 1 && fixture::pauseCloses == 1 && fixture::clears == 0,
            "failed detach closes modal policy but retains native metadata");
    require(p::is_installed() && p::native_key_guard_attached()
        && p::g_handles[0].attached && p::g_targets[0] == realKey,
        "failed detach retains all executable guard ownership for retry");
    fixture::failDetach = false;
    p::uninstall();
    require(fixture::uninstallCalls == 2 && fixture::clears == 1 && !p::is_installed()
        && !p::native_key_guard_attached() && !p::g_handles[0].attached
        && p::g_targets[0] == nullptr && p::g_targets[1] == nullptr,
        "successful retry clears handles, recovery state, exports and native metadata");
    p::request_native_key_guard();
    p::service_native_key_recovery();
    require(fixture::installCalls == 2, "late recovery cannot attach after uninstall");

    require(p::install() && fixture::installCalls == 2 && !p::native_key_guard_attached(),
            "a new native lifecycle again starts with zero handles");
    p::service_native_key_recovery();
    require(fixture::installCalls == 2, "new lifecycle clears a stale retired recovery request");
    p::uninstall();
    require(!p::is_installed() && fixture::clears == 2,
            "zero-handle lifecycle still releases its native metadata");
    std::printf("PASS %u actual enabled native-key recovery/lifecycle checks; no game writes\n",
                fixture::checks);
}
