#include "assert_handler_lifecycle.h"

#include "../../../core/logging/log.h"
#include "../../targets/game/assert_handler.h"
#include "assert_handler_observer.h"

namespace dawn::client::hooks::assert_handler {

SRWLOCK g_lock{SRWLOCK_INIT};
bool g_installed{};

namespace {

[[nodiscard]] bool set_handler(const targets::game::assert_handler::Targets& targets,
                               const void* expected,
                               void* value) {
    if (targets.setter == nullptr || targets.slot == nullptr
        || static_cast<const void*>(*targets.slot) != expected) {
        return false;
    }
    targets.setter(value);
    return static_cast<void*>(*targets.slot) == value;
}

} // namespace

bool install() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_installed) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (!targets::game::assert_handler::is_resolved()) {
        ReleaseSRWLockExclusive(&g_lock);
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=assert stage=install result=fail reason=target");
        return true;
    }
    const targets::game::assert_handler::Targets& resolved = targets::game::assert_handler::get();
    const bool installed = set_handler(resolved, resolved.original, handler_entry_point());
    g_installed = installed;
    ReleaseSRWLockExclusive(&g_lock);
    core::log::write(core::log::Channel::client,
                     installed ? core::log::Level::info : core::log::Level::warn,
                     installed ? "ev=assert stage=install result=ok"
                               : "ev=assert stage=install result=fail reason=slot");
    return installed;
}

bool uninstall() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_installed) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    const targets::game::assert_handler::Targets& resolved = targets::game::assert_handler::get();
    const bool restored = set_handler(resolved, handler_entry_point(), resolved.original);
    g_installed = !restored;
    ReleaseSRWLockExclusive(&g_lock);
    if (!restored) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=assert stage=uninstall result=fail reason=slot");
    }
    return restored;
}

bool is_installed() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool installed = g_installed;
    ReleaseSRWLockShared(&g_lock);
    return installed;
}

} // namespace dawn::client::hooks::assert_handler
