#include "retail_log_lifecycle.h"
#include "retail_log_enqueue_observer.h"
#include "../../../core/logging/log.h"

namespace dawn::client::hooks::retail_log {
SRWLOCK g_lock{SRWLOCK_INIT};
hooking::detour::Handle g_handle{};
bool install() noexcept {
    core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=retail stage=install result=native hooks=0 diagnostic_capture=removed");
    return true;
}
bool uninstall() noexcept { return true; }
bool is_installed() noexcept { return false; }
}
