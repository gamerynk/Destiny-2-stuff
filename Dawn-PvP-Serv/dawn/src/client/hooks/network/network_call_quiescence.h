#pragma once

#include <Windows.h>

namespace dawn::client::hooks::network::quiescence {

[[nodiscard]] LONG active_calls(volatile LONG& counter) noexcept;

// detour removal waits on this, so no busy polling and no early return; a false return
// leaves callers inside bodies that are about to be unmapped
[[nodiscard]] bool wait_for_idle(volatile LONG& counter, DWORD timeoutMilliseconds) noexcept;

#if defined(DAWN_BAP_HOOK_TEST)
struct PauseGate final {
    volatile LONG armed{};
    volatile LONG entered{};
    volatile LONG released{1};
};

void arm(PauseGate& gate) noexcept;

[[nodiscard]] bool wait_for_entry(PauseGate& gate, DWORD timeoutMilliseconds) noexcept;

void release(PauseGate& gate) noexcept;

// the pause ends on its own even if the test that armed it exits early
inline constexpr unsigned kIngressPauseAttempts = 1'000'000'000;

// the spin must stay in the caller: detour removal refuses while a thread sits in the
// protected body, so parking it in another function would let removal succeed
[[nodiscard]] bool consume_arm(PauseGate& gate) noexcept;

[[nodiscard]] bool held(const PauseGate& gate) noexcept;
#endif

} // namespace dawn::client::hooks::network::quiescence
