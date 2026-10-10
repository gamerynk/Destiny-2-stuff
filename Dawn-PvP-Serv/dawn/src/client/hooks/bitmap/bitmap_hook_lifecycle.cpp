#include "bitmap_hook_lifecycle.h"

#include <atomic>

#include "bitmap_ref_guard.h"
#include "../../../core/logging/log.h"

namespace dawn::client::hooks::bitmap {
namespace {

std::atomic_bool g_installed{false};

} // namespace

bool install() noexcept {
#if defined(RESTORATION_BITMAP_GUARD_OMISSION) && RESTORATION_BITMAP_GUARD_OMISSION
    g_installed.store(false, std::memory_order_release);
    core::log::write(core::log::Channel::client, core::log::Level::info,
                    "ev=bitmap stage=install result=omitted variant=native_reference hooks=0");
    return true;
#else
    const bool refGuard = install_bitmap_ref_guard();
    g_installed.store(refGuard, std::memory_order_release);
    return refGuard;
#endif
}

void uninstall() noexcept {
    uninstall_bitmap_ref_guard();
    g_installed.store(false, std::memory_order_release);
}

bool is_installed() noexcept {
    return g_installed.load(std::memory_order_acquire);
}

} // namespace dawn::client::hooks::bitmap
