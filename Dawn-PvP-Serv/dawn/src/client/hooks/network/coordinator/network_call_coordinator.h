#pragma once

#include <Windows.h>

#include "../platform.h"

namespace dawn::client::hooks::network::coordinator {

struct CallLease final {
    void* original{};
    bool accepting{};
};

using CallIngress = void (*)(CallLease&, HookSlot) noexcept;
using CallEgress = void (*)() noexcept;

extern const CallIngress g_callIngress;
extern const CallEgress g_callEgress;

[[nodiscard]] void* ingress_entry_point() noexcept;

[[nodiscard]] void* egress_entry_point() noexcept;

[[nodiscard]] bool idle() noexcept;

[[nodiscard]] bool wait_for_idle(DWORD timeoutMilliseconds) noexcept;

[[nodiscard]] LONG active_calls() noexcept;

[[nodiscard]] bool current_thread_active() noexcept;

#if defined(DAWN_BAP_HOOK_TEST)
namespace testing {

void arm_ingress_pause() noexcept;

[[nodiscard]] bool wait_for_ingress_pause(DWORD timeoutMilliseconds) noexcept;

void release_ingress_pause() noexcept;

} // namespace testing
#endif

} // namespace dawn::client::hooks::network::coordinator
