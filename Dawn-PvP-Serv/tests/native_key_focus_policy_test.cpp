#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "../dawn/src/client/hooks/polled_input/native_key_focus_policy.h"

namespace fixture {
using namespace dawn::client::hooks::polled_input::native_key;
Policy policy;
volatile unsigned char skip{}, windowFocused{1}, suppressed{}, primary{};
volatile unsigned focus{};
bool modal{true}, foreground{true}, guardAttached{};
std::atomic_uint checks{};
unsigned publications{}, fallbackCalls{}, observations{}, depth{};
std::atomic_bool openingDone{};
HANDLE queryAttempt{};
CRITICAL_SECTION queue;
const HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1234));
HWND currentWindow = window;
enum class Reentry { none, ordinary, nativePresent, close };
Reentry reentry{};
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n", reason); std::exit(1); }
}
void enter(unsigned id, int dependency) {
    require(id == 28 && dependency == 0, "exact queue identity/dependency mode");
    if (queryAttempt) SetEvent(queryAttempt);
    EnterCriticalSection(&queue); ++depth;
}
void leave(unsigned id, int dependency) {
    require(id == 28 && dependency == 0 && depth != 0, "balanced queue release");
    --depth; LeaveCriticalSection(&queue);
}
void setPrimary(bool enabled) { primary = enabled ? 1 : 0; }
void publish(void*, HWND target, bool enabled) {
    require(target == window, "genuine publisher uses native window identity");
    require(enabled ? depth != 0 : depth == 0, "gain excludes scans; loss never owns input queue");
    ++publications; focus = enabled ? 1 : 0; primary = enabled ? 1 : 0;
    if (enabled && reentry != Reentry::none) {
        const auto kind = reentry; reentry = Reentry::none;
        auto nested = policy.begin(window, WM_MOUSEMOVE);
        require(depth == 0 && primary == 0, "post-enable reentry pauses before releasing outer queue");
        if (kind == Reentry::nativePresent) {
            skip = 0;
            auto another = policy.begin(window, WM_SIZE);
            require(skip == 1, "subsequent nested ordinary entry reasserts focus suppression");
            policy.end(another);
            require(skip == 1, "inner ordinary exit preserves outer guard");
        }
        if (kind == Reentry::close) { modal = false; primary = 1; }
        policy.end(nested);
    }
}
bool sameForeground(HWND target) { return target == window && foreground; }
bool isModal() { return modal; }
void observe() { ++observations; }
bool currentOwner(HWND target) { return target == currentWindow; }
bool realGuard() { return guardAttached; }
void requestGuard() { ++fallbackCalls; }
Bindings bindings() {
    return {window, GetCurrentThreadId(), nullptr, &skip, &windowFocused, &suppressed,
            &primary, &focus, 28, &enter, &leave, &setPrimary, &publish,
            &sameForeground, &isModal, &observe, &currentOwner, &realGuard, &requestGuard};
}
void reset() {
    require(policy.stop(), "previous scopes fully retired");
    skip = 0; windowFocused = 1; suppressed = 0; primary = 0; focus = 0;
    modal = foreground = true; guardAttached = false; currentWindow = window;
    publications = fallbackCalls = observations = depth = 0; reentry = Reentry::none;
    require(policy.configure(bindings()), "native contract configures only while idle");
}
void openPause() { require(depth != 0, "pause occurs after query drain before modal publish"); primary = 0; }
}

int main() {
    using namespace fixture;
    InitializeCriticalSection(&queue);
    reset();
    primary = 1; modal = false;
    const HANDLE ownerEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const HANDLE releaseOwner = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    queryAttempt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    require(ownerEntered && releaseOwner && queryAttempt, "query drain events created");
    std::thread queryOwner([&] {
        EnterCriticalSection(&queue);
        SetEvent(ownerEntered);
        require(WaitForSingleObject(releaseOwner, 5000) == WAIT_OBJECT_0,
                "query owner receives explicit release");
        LeaveCriticalSection(&queue);
    });
    require(WaitForSingleObject(ownerEntered, 5000) == WAIT_OBJECT_0,
            "native query fixture holds the real queue before opening");
    std::thread opener([&] {
        require(policy.prepare_open(&openPause), "second thread drains admitted query");
        openingDone.store(true, std::memory_order_release);
    });
    require(WaitForSingleObject(queryAttempt, 5000) == WAIT_OBJECT_0,
            "opener reaches genuine queue acquisition while query owner is held");
    require(primary == 1 && publications == 0 && !openingDone.load(std::memory_order_acquire),
            "primary pause and modal publication cannot pass an admitted held query");
    SetEvent(releaseOwner);
    opener.join(); queryOwner.join();
    require(primary == 0 && openingDone.load(std::memory_order_acquire),
            "pause completes only after explicit query release");
    CloseHandle(ownerEntered); CloseHandle(releaseOwner); CloseHandle(queryAttempt);
    queryAttempt = nullptr;
    primary = 1;
    require(policy.prepare_open(&openPause) && primary == 0 && depth == 0,
            "opening drains queries and pauses before publishing modal visibility");
    modal = true;
    auto token = policy.begin(window, WM_MOUSEMOVE);
    require(primary == 0 && focus == 1 && skip == 1 && depth == 0,
            "genuine gain returns paused and original forwards unlocked under guard");
    require(!policy.stop(), "busy shutdown retains admitted native metadata");
    policy.end(token);
    require(skip == 0 && policy.stop(), "scope restores transient byte and shutdown retry succeeds");

    for (const auto kind : {Reentry::ordinary, Reentry::nativePresent, Reentry::close}) {
        reset(); reentry = kind;
        token = policy.begin(window, WM_ACTIVATE);
        require(depth == 0 && skip == 1, "all reentry paths return unlocked with outer guard intact");
        require(primary == (kind == Reentry::close ? 1 : 0),
                "nested close is preserved; open modal stays paused after publisher tail");
        policy.end(token); require(skip == 0, "outer scope restores native original guard");
    }
    reset(); skip = 1; focus = 0;
    token = policy.begin(window, WM_SETFOCUS);
    require(publications == 0 && primary == 0, "native Present's inherited deferral remains honored");
    policy.end(token); require(skip == 1, "inherited native guard restored unchanged");

    reset(); focus = 1; foreground = false;
    token = policy.begin(window, WM_KILLFOCUS);
    require(focus == 0 && primary == 0 && depth == 0, "actual background loss publishes unlocked");
    policy.end(token);
    reset(); focus = 1; primary = 0;
    token = policy.begin(window, WM_MOUSEMOVE); policy.end(token);
    require(primary == 0 && publications == 0, "originally disabled primary is not enabled on no-op focus");

    reset();
    token = policy.begin(window, WM_CAPTURECHANGED, false);
    require(publications == 0 && skip == 1, "renderer preparation defers publication but suppresses native focus head");
    policy.end(token); policy.service_deferred(false);
    require(publications == 0, "publication never runs while preparation permission is false");
    policy.service_deferred(true);
    require(publications == 1 && primary == 0 && skip == 0, "deferred focus publishes after unlocked native forwarding");

    reset();
    for (UINT command = WM_USER; command <= WM_USER + 15; ++command) {
        token = policy.begin(window, command);
        require(!token.admitted && skip == 0 && publications == 0,
                "entire native400..40F command range keeps baseline focus behavior");
    }
    reset(); currentWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x4321));
    token = policy.begin(currentWindow, WM_SIZE);
    require(token.scoped && policy.fallback_requested() && fallbackCalls == 1
                && publications == 0 && skip == 1,
            "authoritative new native-window owner suppresses focus while worker recovery is pending");
    policy.end(token); guardAttached = true;
    token = policy.begin(currentWindow, WM_SIZE);
    require(!token.scoped && fallbackCalls == 2,
            "successful real worker guard permits original native focus without data override");
    policy.end(token);

    reset();
    token = policy.begin(window, WM_MOUSEMOVE);
    require(!policy.configure(bindings()), "cannot replace bindings across an admitted native message");
    policy.end(token); require(policy.stop(), "final metadata teardown drains scopes");
    token = policy.begin(window, WM_MOUSEMOVE);
    require(!token.admitted && skip == 0, "late entry never touches retired native addresses");
    policy.end(token);
    DeleteCriticalSection(&queue);
    std::printf("PASS %u actual native-key policy/lifetime/reentry checks; includes two-thread query drain; no game writes\n", checks.load());
}
