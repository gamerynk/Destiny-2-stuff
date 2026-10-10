#include "network_call_coordinator.h"

#include "../network_call_quiescence.h"

namespace dawn::client::hooks::network {

volatile LONG g_activeCalls{};

} // namespace dawn::client::hooks::network

namespace dawn::client::hooks::network::coordinator {
namespace {

thread_local unsigned g_callDepth{};

#if defined(DAWN_BAP_HOOK_TEST)
quiescence::PauseGate g_pauseGate;

#endif

__declspec(noinline) void
ingress_body(CallLease& lease, HookSlot slot) noexcept {
#if defined(DAWN_BAP_HOOK_TEST)
    // the spin stays here, not in a helper: detour removal refuses while a thread sits in this
    // body, and the paused call is what proves it
    if (quiescence::consume_arm(g_pauseGate)) {
        for (unsigned attempt = 0;
             attempt < quiescence::kIngressPauseAttempts && quiescence::held(g_pauseGate);
             ++attempt) {
            YieldProcessor();
        }
    }
#endif
    if (g_callDepth == 0) {
        (void)InterlockedIncrement(&g_activeCalls);
    }

    AcquireSRWLockShared(&g_lock);
    lease.original = original<void*>(slot);
    lease.accepting = g_accepting[static_cast<std::size_t>(slot)];
    ++g_callDepth;
    ReleaseSRWLockShared(&g_lock);
}

__declspec(noinline) void egress_body() noexcept {
    if (g_callDepth == 1) {
        // this non-leaf call keeps unwind data present while the active token is still held
        WakeByAddressAll(const_cast<LONG*>(&g_activeCalls));
    }
    --g_callDepth;
    if (g_callDepth == 0) {
        (void)InterlockedDecrement(&g_activeCalls);
    }
}

} // namespace

const CallIngress g_callIngress = &ingress_body;
const CallEgress g_callEgress = &egress_body;

void* ingress_entry_point() noexcept {
    return reinterpret_cast<void*>(&ingress_body);
}

void* egress_entry_point() noexcept {
    return reinterpret_cast<void*>(&egress_body);
}

bool idle() noexcept {
    return active_calls() == 0;
}

bool wait_for_idle(DWORD timeoutMilliseconds) noexcept {
    return quiescence::wait_for_idle(g_activeCalls, timeoutMilliseconds);
}

LONG active_calls() noexcept {
    return quiescence::active_calls(g_activeCalls);
}

bool current_thread_active() noexcept {
    return g_callDepth != 0;
}

#if defined(DAWN_BAP_HOOK_TEST)
namespace testing {

void arm_ingress_pause() noexcept {
    quiescence::arm(g_pauseGate);
}

bool wait_for_ingress_pause(DWORD timeoutMilliseconds) noexcept {
    return quiescence::wait_for_entry(g_pauseGate, timeoutMilliseconds);
}

void release_ingress_pause() noexcept {
    quiescence::release(g_pauseGate);
}

} // namespace testing
#endif

} // namespace dawn::client::hooks::network::coordinator
