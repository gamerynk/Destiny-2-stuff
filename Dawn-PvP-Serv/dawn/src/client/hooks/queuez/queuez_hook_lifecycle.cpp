#include <atomic>
#include "../../../core/logging/log.h"
#include "queuez_hook_lifecycle.h"

namespace dawn::client::hooks::queuez {
namespace { std::atomic_bool g_nativeSubscriptionReady{false}; }

bool native_subscription_requested() noexcept { return true; }

bool install() noexcept {
    const bool already = g_nativeSubscriptionReady.exchange(true, std::memory_order_acq_rel);
    if (!already) core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=queuez stage=subscription_policy policy=native active_detours=0 paired_server_required=1 failure=native_transport");
    return true;
}

void uninstall() noexcept { g_nativeSubscriptionReady.store(false, std::memory_order_release); }
bool is_installed() noexcept { return g_nativeSubscriptionReady.load(std::memory_order_acquire); }

} // namespace dawn::client::hooks::queuez
