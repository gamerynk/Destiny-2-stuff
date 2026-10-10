#include "../../core/logging/log.h"
#include "../content/investment/worker.h"
#include "../hooks/assert_handler/assert_handler_lifecycle.h"
#include "../hooks/bitmap/bitmap_hook_lifecycle.h"
#include "../hooks/bootflow/bootflow_hook_lifecycle.h"
#include "../hooks/config_getter/config_getter_lifecycle.h"
#include "../hooks/cursor/runtime.h"
#include "../hooks/graphics/graphics_hook_lifecycle.h"
#include "../hooks/network/runtime.h"
#include "../hooks/package_trust/package_trust_bypass.h"
#include "../hooks/polled_input/runtime.h"
#include "../hooks/queuez/queuez_hook_lifecycle.h"
#include "../hooks/retail_log/retail_log_lifecycle.h"
#include "../hooks/teleport/runtime.h"
#include "../targets/game.h"
#include "../targets/steam_targets.h"
#include "../ui/runtime/client_ui_module_runtime.h"
#include "../ui/weapons/weapon_acquire_client.h"
#include "internal.h"
#include "runtime.h"

namespace dawn::client {

bool initialize(void*) noexcept {
    return ui::runtime::initialize();
}

bool shutdown() noexcept {
    AcquireSRWLockExclusive(&runtime::g_lock);
    // cursor capture must drain on its main-window owner while both input subclasses exist;
    // this only signals and reads completion, never forwards native/Win32 work here
    if (!hooks::cursor::prepare_native_tail_shutdown()) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
                         "ev=shutdown stage=cursor_owner result=pending owner_release=1");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    if (!hooks::graphics::uninstall()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=shutdown stage=graphics_hooks result=fail");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    // detached after presentation so no later frame can apply the cursor policy
    hooks::cursor::uninstall();
    if (hooks::cursor::is_installed()) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
                         "ev=shutdown stage=cursor_hooks result=pending retained=1");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    ui::weapons::acquire_client::shutdown();
    hooks::polled_input::uninstall();
    // the native keyboard table belongs to the camera thread; keep that callback attached until
    // it has restored any outstanding lease on its owner thread
    if (hooks::polled_input::is_installed()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=shutdown stage=polled_input result=pending owner_release=1");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    if (!hooks::network::uninstall()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=shutdown stage=network_hooks result=fail");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    if (!hooks::package_trust::uninstall()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=shutdown stage=package_trust result=fail");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    hooks::bitmap::uninstall();
    if (!hooks::bootflow::uninstall()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=shutdown stage=bootflow_hooks result=fail retained=1");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    if (!hooks::teleport::uninstall()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=shutdown stage=player_phase_hooks result=fail retained=1");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    hooks::queuez::uninstall();
    if (!hooks::config_getter::uninstall()) {
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    if (!hooks::assert_handler::uninstall()) {
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    if (!hooks::retail_log::uninstall()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=shutdown stage=retail_log result=fail");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }
    content::investment::worker::reset();
    targets::steam::clear();
    if (runtime::g_platformModule != nullptr) {
        if (FreeLibrary(runtime::g_platformModule) == FALSE) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::error,
                             "ev=shutdown stage=steam_module result=fail");
            ReleaseSRWLockExclusive(&runtime::g_lock);
            return false;
        }
        runtime::g_platformModule = nullptr;
    }
    targets::game::retail_log::clear();
    targets::game::content::clear();
    targets::game::network::clear();
    runtime::g_mainStage = runtime::StageState::pending;
    runtime::g_graphicsStage = runtime::StageState::pending;
    runtime::g_platformStage = runtime::StageState::pending;
    ui::runtime::shutdown();
    core::log::write(core::log::Channel::client, core::log::Level::info, "ev=shutdown result=ok");
    ReleaseSRWLockExclusive(&runtime::g_lock);
    return true;
}

} // namespace dawn::client
