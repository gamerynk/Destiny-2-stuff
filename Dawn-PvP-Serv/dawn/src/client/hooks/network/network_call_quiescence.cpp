#include "network_call_quiescence.h"

#include <algorithm>

namespace dawn::client::hooks::network::quiescence {
namespace {

// 10 ms slices close the wake race before the decrement without busy polling
constexpr DWORD kWaitSliceMilliseconds = 10;

} // namespace

LONG active_calls(volatile LONG& counter) noexcept {
    return InterlockedCompareExchange(&counter, 0, 0);
}

bool wait_for_idle(volatile LONG& counter, DWORD timeoutMilliseconds) noexcept {
    const ULONGLONG started = GetTickCount64();
    LONG observed = active_calls(counter);
    while (observed != 0) {
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (elapsed >= timeoutMilliseconds) {
            return false;
        }
        const auto remaining = static_cast<DWORD>(
            std::min<ULONGLONG>(timeoutMilliseconds - elapsed, kWaitSliceMilliseconds));
        if (WaitOnAddress(&counter, &observed, sizeof observed, remaining) == FALSE
            && GetLastError() != ERROR_TIMEOUT) {
            return false;
        }
        observed = active_calls(counter);
    }
    return true;
}

#if defined(DAWN_BAP_HOOK_TEST)
void arm(PauseGate& gate) noexcept {
    (void)InterlockedExchange(&gate.entered, 0);
    (void)InterlockedExchange(&gate.released, 0);
    (void)InterlockedExchange(&gate.armed, 1);
}

bool wait_for_entry(PauseGate& gate, DWORD timeoutMilliseconds) noexcept {
    const ULONGLONG started = GetTickCount64();
    while (InterlockedCompareExchange(&gate.entered, 0, 0) == 0) {
        if (GetTickCount64() - started >= timeoutMilliseconds) {
            return false;
        }
        Sleep(1);
    }
    return true;
}

void release(PauseGate& gate) noexcept {
    (void)InterlockedExchange(&gate.released, 1);
}

bool consume_arm(PauseGate& gate) noexcept {
    if (InterlockedCompareExchange(&gate.armed, 0, 1) != 1) {
        return false;
    }
    (void)InterlockedExchange(&gate.entered, 1);
    return true;
}

bool held(const PauseGate& gate) noexcept {
    return gate.released == 0;
}
#endif

} // namespace dawn::client::hooks::network::quiescence
