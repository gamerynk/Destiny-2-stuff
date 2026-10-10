#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "../dawn/src/client/runtime/client_runtime_lifecycle.cpp"

namespace fixture {
unsigned checks{};
bool cursorDrained{}, graphicsDetached{true}, cursorAttached{true}, cursorDetachSucceeds{};
bool keysAttached{}, networkDetached{true};
std::vector<std::string_view> calls;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void record(std::string_view call) { calls.push_back(call); }
void expect(std::initializer_list<std::string_view> expected) {
    require(calls == std::vector<std::string_view>(expected), "shutdown order matches owner contract");
}
bool run() {
    calls.clear();
    const bool result = dawn::client::shutdown();
    require(TryAcquireSRWLockExclusive(&dawn::client::runtime::g_lock) != FALSE,
            "every shutdown return releases the coordinator lock");
    ReleaseSRWLockExclusive(&dawn::client::runtime::g_lock);
    return result;
}
void targets_retained() {
    namespace r = dawn::client::runtime;
    require(r::g_mainStage == r::StageState::active
        && r::g_graphicsStage == r::StageState::active
        && r::g_platformStage == r::StageState::active,
        "pending cleanup retains stage metadata");
}
}

namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace dawn::client::runtime {
SRWLOCK g_lock{SRWLOCK_INIT};
StageState g_mainStage{StageState::active}, g_graphicsStage{StageState::active},
    g_platformStage{StageState::active};
HMODULE g_platformModule{};
}
namespace dawn::client::movement {
void initialize(void*) noexcept {}
void shutdown() noexcept { fixture::record("movement"); }
}
namespace dawn::client::ui::runtime {
bool initialize() noexcept { return true; }
void shutdown() noexcept { fixture::record("ui"); }
}
namespace dawn::client::ui::weapons::acquire_client {
void shutdown() noexcept { fixture::record("weapon_requests"); }
}
namespace dawn::client::hooks::cursor {
bool prepare_native_tail_shutdown() noexcept { fixture::record("cursor_drain"); return fixture::cursorDrained; }
void uninstall() noexcept {
    fixture::record("cursor_detach");
    if (fixture::cursorDetachSucceeds) fixture::cursorAttached = false;
}
bool is_installed() noexcept { fixture::record("cursor_status"); return fixture::cursorAttached; }
}
namespace dawn::client::hooks::graphics {
bool uninstall() noexcept { fixture::record("graphics"); return fixture::graphicsDetached; }
}
namespace dawn::client::hooks::polled_input {
void uninstall() noexcept { fixture::record("keys"); }
bool is_installed() noexcept { return fixture::keysAttached; }
}
namespace dawn::client::hooks::network {
bool uninstall() noexcept { fixture::record("network"); return fixture::networkDetached; }
}
namespace dawn::client::hooks::package_trust {
bool uninstall() noexcept { fixture::record("packages"); return true; }
}
namespace dawn::client::hooks::bitmap { void uninstall() noexcept { fixture::record("bitmap"); } }
namespace dawn::client::hooks::bootflow { bool uninstall() noexcept { fixture::record("bootflow"); return true; } }
namespace dawn::client::hooks::teleport { bool uninstall() noexcept { fixture::record("physics"); return true; } }
namespace dawn::client::hooks::queuez { void uninstall() noexcept { fixture::record("queuez"); } }
namespace dawn::client::hooks::config_getter { bool uninstall() noexcept { fixture::record("config"); return true; } }
namespace dawn::client::hooks::assert_handler { bool uninstall() noexcept { fixture::record("assert"); return true; } }
namespace dawn::client::hooks::retail_log { bool uninstall() noexcept { fixture::record("logs"); return true; } }
namespace dawn::client::content::investment::worker { void reset() noexcept { fixture::record("investment"); } }
namespace dawn::client::targets::steam { void clear() noexcept { fixture::record("steam_targets"); } }
namespace dawn::client::targets::game::retail_log { void clear() noexcept { fixture::record("log_targets"); } }
namespace dawn::client::targets::game::content { void clear() noexcept { fixture::record("content_targets"); } }
namespace dawn::client::targets::game::network { void clear() noexcept { fixture::record("network_targets"); } }

int main() {
    using namespace fixture;
    require(!run(), "cursor owner pending defers shutdown");
    expect({"cursor_drain"});
    targets_retained();
    cursorDrained = true; graphicsDetached = false;
    require(!run(), "graphics pending preserves cursor lifecycle");
    expect({"cursor_drain", "graphics"});
    require(cursorAttached, "cursor remains callable until graphics finishes");
    targets_retained();
    graphicsDetached = true;
    require(!run(), "failed cursor detach defers remaining teardown");
    expect({"cursor_drain", "graphics", "cursor_detach", "cursor_status"});
    targets_retained();
    cursorDetachSucceeds = true; keysAttached = true;
    require(!run(), "key owner pending still defers later teardown");
    expect({"cursor_drain", "graphics", "cursor_detach", "cursor_status", "weapon_requests", "keys"});
    targets_retained();
    keysAttached = false; networkDetached = false;
    require(!run(), "later teardown failure remains retryable");
    expect({"cursor_drain", "graphics", "cursor_detach", "cursor_status", "weapon_requests", "keys", "network"});
    targets_retained();
    networkDetached = true;
    require(run(), "fully drained and detached owners permit shutdown");
    expect({"cursor_drain", "graphics", "cursor_detach", "cursor_status", "weapon_requests", "keys", "network",
        "packages", "bitmap", "bootflow", "physics", "queuez", "config", "assert", "logs", "investment",
        "steam_targets", "log_targets", "content_targets", "network_targets", "ui", "movement"});
    namespace r = dawn::client::runtime;
    require(r::g_mainStage == r::StageState::pending && r::g_graphicsStage == r::StageState::pending
        && r::g_platformStage == r::StageState::pending, "successful shutdown clears stage metadata last");
    std::printf("PASS %u cursor shutdown ordering checks\n", checks);
}
