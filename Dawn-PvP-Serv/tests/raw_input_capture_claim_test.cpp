#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <thread>

#include "../dawn/src/core/ui/runtime/ui_visibility_runtime.h"

namespace fixture {
std::atomic_uint originalInputs{}, defaultInputs{}, originalOther{};
std::atomic_bool holdNext{};
std::atomic<HWND> window{};
std::latch* entered{};
std::latch* released{};
HANDLE capturedEvent{};
HANDLE readyEvent{};
constexpr UINT kEcho = WM_APP + 1;
constexpr UINT kRecreate = WM_APP + 2;
constexpr UINT kStop = WM_APP + 3;
constexpr LRESULT kOriginalInputResult = 0x1234;
constexpr LRESULT kOriginalOtherResult = 0x5678;
std::atomic<WPARAM> lastDefaultWord{};
std::atomic<LPARAM> lastDefaultValue{};
std::atomic<WPARAM> lastOriginalWord{};
std::atomic<LPARAM> lastOriginalValue{};
std::atomic<WPARAM> lastOtherWord{};
std::atomic<LPARAM> lastOtherValue{};

LRESULT CALLBACK native_original(HWND owner, UINT message, WPARAM word, LPARAM value);
HWND create_hidden_window() {
    return CreateWindowExW(0, L"Tiger Input Window", L"raw capture fixture", WS_POPUP,
        0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}
LRESULT CALLBACK native_original(HWND owner, UINT message, WPARAM word, LPARAM value) {
    if (message == WM_INPUT) {
        originalInputs.fetch_add(1, std::memory_order_acq_rel);
        lastOriginalWord.store(word); lastOriginalValue.store(value);
        if (holdNext.exchange(false)) {
            entered->count_down();
            released->wait();
        }
        return kOriginalInputResult;
    }
    if (message == kEcho) {
        originalOther.fetch_add(1);
        lastOtherWord.store(word); lastOtherValue.store(value);
        return kOriginalOtherResult;
    }
    if (message == kRecreate) {
        (void)DestroyWindow(owner);
        window.store(create_hidden_window());
        return window.load() ? 1 : 0;
    }
    if (message == kStop) {
        (void)DestroyWindow(owner);
        PostQuitMessage(0);
        return 1;
    }
    return DefWindowProcW(owner, message, word, value);
}

LRESULT WINAPI counted_default(HWND owner, UINT message, WPARAM word, LPARAM value) {
    if (message == WM_INPUT) {
        defaultInputs.fetch_add(1, std::memory_order_acq_rel);
        lastDefaultWord.store(word); lastDefaultValue.store(value);
    }
    const LRESULT result = DefWindowProcW(owner, message, word, value);
    if (message == WM_INPUT && capturedEvent) (void)SetEvent(capturedEvent);
    return result;
}

void raw_owner_thread() {
    WNDCLASSW definition{};
    definition.lpfnWndProc = native_original;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpszClassName = L"Tiger Input Window";
    if (!RegisterClassW(&definition)) std::abort();
    window.store(create_hidden_window());
    (void)SetEvent(readyEvent);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
    (void)UnregisterClassW(definition.lpszClassName, definition.hInstance);
}
}

namespace dawn::client::hooks::polled_input {
bool native_pause_owned() noexcept { return true; }
}

#define DefWindowProcW fixture::counted_default
#include "../dawn/src/client/hooks/graphics/input/graphics_raw_input_window.cpp"
#undef DefWindowProcW

namespace {
namespace core_ui = dawn::core::ui::runtime;
namespace raw = dawn::client::hooks::graphics::input;
int checks{};
void check(bool value) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL raw claim check%d\n", checks); std::abort(); }
}
bool wait(HANDLE event) { return WaitForSingleObject(event, 5000) == WAIT_OBJECT_0; }
LRESULT send_raw() { return SendMessageW(fixture::window.load(), WM_INPUT, 0, 0); }

void check_default_policy() {
    const unsigned int nativeBefore = fixture::originalInputs.load();
    const unsigned int defaultBefore = fixture::defaultInputs.load();
    const LRESULT result = send_raw();
#if defined(RESTORATION_NATIVE_RAW_MOUSE_PAUSE) && RESTORATION_NATIVE_RAW_MOUSE_PAUSE
    check(result == fixture::kOriginalInputResult);
    check(fixture::originalInputs.load() == nativeBefore + 1);
    check(fixture::defaultInputs.load() == defaultBefore);
#else
    check(result == 0);
    check(fixture::originalInputs.load() == nativeBefore);
    check(fixture::defaultInputs.load() == defaultBefore + 1);
#endif
}
}

int main() {
    fixture::capturedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fixture::readyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    check(fixture::capturedEvent && fixture::readyEvent);
    std::thread nativeOwner{fixture::raw_owner_thread};
    check(wait(fixture::readyEvent) && fixture::window.load());

    core_ui::shutdown();
    const auto shutdownGeneration = core_ui::snapshot().generation;
    check(shutdownGeneration != 0);
    check(core_ui::initialize({true, VK_INSERT}));
    const auto initial = core_ui::snapshot();
    check(initial.generation > shutdownGeneration && !initial.visible);
    check(!core_ui::initialize({true, 0}));
    check(core_ui::snapshot().generation == initial.generation);
    check(!core_ui::toggle_for_key('Q'));
    check(core_ui::snapshot().generation == initial.generation);
    check(raw::install_raw_input_window());
    const auto binding = raw::snapshot_raw_input();
    check(binding.installed && binding.bindingGeneration != 0
        && binding.window == fixture::window.load());
    check(!binding.claimMatched && !binding.drained);
    check(raw::install_raw_input_window());
    check(raw::snapshot_raw_input().bindingGeneration == binding.bindingGeneration);
    raw::RawCaptureClaim rejected;
    check(!raw::claim_raw_input_capture(initial.generation, binding.bindingGeneration, rejected));
    check(rejected.identity == 0);

    std::latch pendingEntered{1}, pendingReturned{1};
    fixture::entered = &pendingEntered; fixture::released = &pendingReturned;
    fixture::holdNext.store(true);
    check(PostMessageW(fixture::window.load(), WM_INPUT, 0, 0) != FALSE);
    pendingEntered.wait();
    raw::RawOpeningLease opening;
    SetLastError(8732);
    check(raw::claim_raw_opening(initial.generation, binding.bindingGeneration, opening));
    check(GetLastError() == 8732 && opening.identity != 0);
    check(!core_ui::snapshot().visible);
    const auto pending = raw::snapshot_raw_opening(opening);
    check(pending.matched && !pending.drained && pending.activeCallbacks == 1);
    raw::RawOpeningLease other;
    check(!raw::claim_raw_opening(initial.generation, binding.bindingGeneration, other));
    check(other.identity == 0);
    auto wrongOpening = opening; ++wrongOpening.identity;
    check(!raw::release_raw_opening(wrongOpening));
    (void)ResetEvent(fixture::capturedEvent);
    check(PostMessageW(fixture::window.load(), WM_INPUT, 0, 0) != FALSE);
    pendingReturned.count_down();
    check(wait(fixture::capturedEvent));
    check(SendMessageW(fixture::window.load(), fixture::kEcho, 0, 0) == fixture::kOriginalOtherResult);
    check(raw::snapshot_raw_opening(opening).drained && !core_ui::snapshot().visible);
    check(send_raw() == 0);
    core_ui::VisibilitySnapshot committed;
    check(!core_ui::open_for_key('Q', initial.generation, committed));
    check(!committed.visible && committed.generation == 0);
    check(core_ui::open_for_key(VK_INSERT, initial.generation, committed));
    check(committed.visible && committed.generation > initial.generation);
    check(raw::snapshot_raw_opening(opening).drained);
    check(send_raw() == 0);
    check(!core_ui::open_for_key(VK_INSERT, initial.generation, committed));
    check(core_ui::toggle_for_key(VK_INSERT));
    check(send_raw() == 0);
    check(raw::release_raw_opening(opening));
    check(!raw::snapshot_raw_opening(opening).matched);
    check(send_raw() == fixture::kOriginalInputResult);
    const auto pendingClosed = core_ui::snapshot();
    check(raw::claim_raw_opening(pendingClosed.generation, binding.bindingGeneration, other));
    check(other.identity > opening.identity);
    check(!raw::release_raw_opening(opening));
    check(raw::snapshot_raw_opening(other).drained);
    check(raw::release_raw_opening(other));
    check(!raw::claim_raw_opening(initial.generation, binding.bindingGeneration, opening));
    check(!raw::claim_raw_opening(pendingClosed.generation, binding.bindingGeneration + 1, opening));

    std::latch admitted{1}, release{1};
    fixture::entered = &admitted; fixture::released = &release; fixture::holdNext.store(true);
    const unsigned int nativeBefore = fixture::originalInputs.load();
    check(PostMessageW(fixture::window.load(), WM_INPUT, 0, 0) != FALSE);
    admitted.wait();
    check(raw::snapshot_raw_input().activeCallbacks == 1);
    check(core_ui::toggle_for_key(VK_INSERT));
    const auto opened = core_ui::snapshot();
    check(opened.visible && opened.generation > initial.generation);
    SetLastError(7654);
    raw::RawCaptureClaim claim;
    check(raw::claim_raw_input_capture(opened.generation, binding.bindingGeneration, claim));
    check(GetLastError() == 7654);
    check(claim.identity != 0 && claim.visibilityGeneration == opened.generation);
    raw::RawCaptureClaim duplicate;
    check(raw::claim_raw_input_capture(opened.generation, binding.bindingGeneration, duplicate));
    check(duplicate == claim);
    const auto held = raw::snapshot_raw_input();
    check(held.claimMatched && held.activeCallbacks == 1 && !held.drained);
    check(GetLastError() == 7654);
    const unsigned int defaultBefore = fixture::defaultInputs.load();
    (void)ResetEvent(fixture::capturedEvent);
    check(PostMessageW(fixture::window.load(), WM_INPUT, 0, 0) != FALSE);
    release.count_down();
    check(wait(fixture::capturedEvent));
    check(SendMessageW(fixture::window.load(), fixture::kEcho, 7, 9) == fixture::kOriginalOtherResult);
    check(fixture::lastOtherWord.load() == 7 && fixture::lastOtherValue.load() == 9);
    check(fixture::originalInputs.load() == nativeBefore + 1);
    check(fixture::lastOriginalWord.load() == 0 && fixture::lastOriginalValue.load() == 0);
    check(fixture::defaultInputs.load() == defaultBefore + 1);
    check(fixture::lastDefaultWord.load() == 0 && fixture::lastDefaultValue.load() == 0);
    const auto drained = raw::snapshot_raw_input();
    check(drained.claimMatched && drained.drained && drained.activeCallbacks == 0);

    auto wrong = claim; ++wrong.identity;
    check(!raw::release_raw_input_capture(wrong));
    check(raw::snapshot_raw_input().claim == claim);
    check(raw::release_raw_input_capture(claim));
    check(!raw::snapshot_raw_input().claimMatched && !raw::snapshot_raw_input().drained);
    check_default_policy();

    check(raw::claim_raw_input_capture(opened.generation, binding.bindingGeneration, claim));
    check(core_ui::toggle_for_key(VK_INSERT));
    const auto closed = core_ui::snapshot();
    check(!closed.visible && closed.generation > opened.generation);
    check(!raw::snapshot_raw_input().claimMatched);
    const unsigned int closedNative = fixture::originalInputs.load();
    check(send_raw() == fixture::kOriginalInputResult);
    check(fixture::originalInputs.load() == closedNative + 1);
    check(core_ui::toggle_for_key(VK_INSERT));
    const auto reopened = core_ui::snapshot();
    check(reopened.visible && reopened.generation > closed.generation);
    check(!raw::snapshot_raw_input().claimMatched);
    check_default_policy();
    raw::RawCaptureClaim newClaim;
    check(raw::claim_raw_input_capture(reopened.generation, binding.bindingGeneration, newClaim));
    check(newClaim.identity != claim.identity);
    check(!raw::release_raw_input_capture(claim));
    check(raw::snapshot_raw_input().claim == newClaim);

    check(core_ui::toggle_for_key(VK_INSERT));
    std::latch oldEntered{1}, oldRelease{1};
    fixture::entered = &oldEntered; fixture::released = &oldRelease; fixture::holdNext.store(true);
    check(PostMessageW(fixture::window.load(), WM_INPUT, 0, 0) != FALSE);
    oldEntered.wait();
    check(raw::snapshot_raw_input().activeCallbacks == 1);
    raw::RawOpeningLease retiringOpening;
    check(raw::claim_raw_opening(core_ui::snapshot().generation,
                                binding.bindingGeneration, retiringOpening));
    check(raw::snapshot_raw_opening(retiringOpening).activeCallbacks == 1);
    check(!raw::uninstall_raw_input_window());
    const auto retired = raw::snapshot_raw_input();
    check(!retired.installed && retired.bindingGeneration > binding.bindingGeneration);
    check(retired.activeCallbacks == 1 && !retired.claimMatched && !retired.drained);
    const auto retiredOpening = raw::snapshot_raw_opening(retiringOpening);
    check(!retiredOpening.matched && !retiredOpening.drained && retiredOpening.activeCallbacks == 1);
    check(!raw::release_raw_opening(retiringOpening));
    check(!raw::install_raw_input_window());
    check(!raw::release_raw_input_capture(newClaim));
    oldRelease.count_down();
    check(SendMessageW(fixture::window.load(), fixture::kEcho, 0, 0) == fixture::kOriginalOtherResult);
    check(raw::snapshot_raw_input().activeCallbacks == 0);
    check(raw::uninstall_raw_input_window());
    check(SendMessageW(fixture::window.load(), fixture::kRecreate, 0, 0) == 1);
    check(raw::install_raw_input_window());
    const auto rebound = raw::snapshot_raw_input();
    check(rebound.installed && rebound.bindingGeneration > retired.bindingGeneration);
    check(core_ui::toggle_for_key(VK_INSERT));
    const auto thirdOpen = core_ui::snapshot();
    check(!raw::claim_raw_input_capture(thirdOpen.generation, binding.bindingGeneration, rejected));
    check(!raw::release_raw_input_capture(newClaim));
    check(raw::claim_raw_input_capture(thirdOpen.generation, rebound.bindingGeneration, newClaim));
    check(raw::snapshot_raw_input().drained);

    core_ui::shutdown();
    const auto firstShutdown = core_ui::snapshot();
    core_ui::shutdown();
    check(core_ui::snapshot().generation > firstShutdown.generation);
    check(!raw::snapshot_raw_input().claimMatched);
    check(core_ui::initialize({true, VK_INSERT}));
    check(core_ui::snapshot().generation > firstShutdown.generation);
    check(!raw::snapshot_raw_input().claimMatched);
    check(raw::release_raw_input_capture(newClaim));
    check(raw::uninstall_raw_input_window());
    check(SendMessageW(fixture::window.load(), fixture::kStop, 0, 0) == 1);
    nativeOwner.join();
    (void)CloseHandle(fixture::capturedEvent); (void)CloseHandle(fixture::readyEvent);
    std::printf("raw input capture claim PASS %d checks; native pause forwarding=%d\n", checks,
#if defined(RESTORATION_NATIVE_RAW_MOUSE_PAUSE) && RESTORATION_NATIVE_RAW_MOUSE_PAUSE
        1
#else
        0
#endif
    );
}
