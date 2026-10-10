#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../core/logging/log.h"
#include "../../core/ui/notice/ui_notice_overlay.h"
#include "../../core/ui/runtime/ui_visibility_runtime.h"
#include "../content/bootstrap/bootstrap_token_publish.h"
#include "../content/investment/worker.h"
#include "../executable/image.h"
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
#include "../patterns/registry.h"
#include "../targets/game.h"
#include "internal.h"
#include "runtime.h"

namespace dawn::client::runtime {

SRWLOCK g_lock{SRWLOCK_INIT};
StageState g_mainStage{StageState::pending};
StageState g_graphicsStage{StageState::pending};
StageState g_platformStage{StageState::pending};
HMODULE g_platformModule{};

namespace {

struct GameImageRanges {
    executable::ExecutableImage executable;
    std::array<patterns::ImageRange, executable::kPeSectionLimit> ranges{};
};

[[nodiscard]] bool inspect_game_image(GameImageRanges& output) noexcept {
    output = {};
    if (!executable::inspect_main_module(output.executable)) {
        return false;
    }
    for (std::size_t index = 0; index < output.executable.count; ++index) {
        output.ranges[index] = patterns::ImageRange{output.executable.sections[index]};
    }
    return true;
}

[[nodiscard]] std::span<patterns::ImageRange> ranges(GameImageRanges& image) noexcept {
    return std::span(image.ranges.data(), image.executable.count);
}

void report_resolve_failure() noexcept {
    const auto failure = targets::game::resolution::last_failure();
    if (failure == targets::game::resolution::Failure::networkDerive) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=activate stage=game_targets reason=network_derive result=fail");
        return;
    }
    if (failure == targets::game::resolution::Failure::contentDerive) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=activate stage=game_targets reason=content_derive result=fail");
        return;
    }
    const std::string_view name = targets::game::resolution::last_failed_signature();
    std::array<char, 128> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=activate stage=game_targets reason=signature name=%.*s "
                                      "result=fail",
                                      static_cast<int>(name.size()),
                                      name.data());
    if (written <= 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=activate stage=game_targets reason=signature result=fail");
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(
        core::log::Channel::client, core::log::Level::error, std::string_view(line.data(), length));
}

void clear_game_targets() noexcept {
    targets::game::content::clear();
    targets::game::network::clear();
}

[[nodiscard]] bool activate_required_main_locked() noexcept {
    GameImageRanges gameImage;
    if (!inspect_game_image(gameImage)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=activate stage=game_image result=fail");
        clear_game_targets();
        return false;
    }
    const std::span<patterns::ImageRange> imageRanges = ranges(gameImage);
    if (!targets::game::resolution::resolve(imageRanges)) {
        report_resolve_failure();
        return false;
    }
    // Steam init installs package trust before base-package registration; keep this idempotent
    // check beside the other main-image hooks so activation also verifies ownership
    if (!hooks::package_trust::install()) {
        clear_game_targets();
        return false;
    }
    // the SignOn config blob carries this token; it must reach State before any hook owns the
    // resolved targets, since extraction can't recover from a missing bootstrap token
    if (!content::bootstrap::publish_token()) {
        (void)hooks::package_trust::uninstall();
        clear_game_targets();
        return false;
    }
    if (!hooks::network::install_game()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=activate stage=game_network result=fail");
        if (!hooks::network::has_game_ownership()) {
            (void)hooks::package_trust::uninstall();
            clear_game_targets();
        }
        return false;
    }

    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=activate stage=game_network result=ok");
    const bool packageKeys = targets::game::packages::is_resolved();
    core::log::write(core::log::Channel::client,
                     packageKeys ? core::log::Level::info : core::log::Level::warn,
                     packageKeys ? "ev=activate stage=package_keys result=ok"
                                 : "ev=activate stage=package_keys result=fail");
    // a native sobject creation failure captures a bounded call stack, at most four times per
    // process; non-required so a diagnostic miss cannot block activation
    (void)hooks::retail_log::install();
    (void)hooks::assert_handler::install();
    if (content::bootstrap::native_configuration_ready()) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
                         "ev=config_getter stage=native_signon result=selected detours=0 acknowledged=1");
    } else {
        (void)hooks::config_getter::install();
    }
    (void)hooks::bootflow::install();
    (void)hooks::teleport::install();
    (void)hooks::queuez::install();
    // the bitmap reference guard swaps in the none sentinel for a reference outside tag space;
    // without it the widget's stored-reference reader faults
    (void)hooks::bitmap::install();
    content::investment::worker::activate();
    return true;
}

} // namespace

} // namespace dawn::client::runtime

namespace dawn::client {

bool activate_main_once() noexcept {
    AcquireSRWLockExclusive(&runtime::g_lock);
    if (runtime::g_mainStage != runtime::StageState::pending) {
        const bool active = runtime::g_mainStage == runtime::StageState::active;
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return active;
    }
    core::log::write(
        core::log::Channel::client, core::log::Level::debug, "ev=activate stage=main phase=begin");
    const std::uint64_t startedTick = GetTickCount64();
    const bool active = runtime::activate_required_main_locked();
    core::log::write_elapsed(core::log::Channel::client,
                             "ev=activate stage=main phase=complete",
                             startedTick,
                             active ? "ok" : "fail");
    if (!active) {
        // a failed sweep latches too: repeating it stalls the frame loop for nothing
        runtime::g_mainStage = runtime::StageState::failed;
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=activate stage=main result=fail");
        // the boot cannot reach orbit after this, so tell the user rather than leave them waiting
        core::ui::notice::raise(
            "Restoration could not attach to the game. The boot will not finish.");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }

    runtime::g_mainStage = runtime::StageState::active;
    core::log::write(
        core::log::Channel::client, core::log::Level::info, "ev=activate stage=main result=ok");
    ReleaseSRWLockExclusive(&runtime::g_lock);
    return true;
}

bool activate_graphics_once() noexcept {
    AcquireSRWLockExclusive(&runtime::g_lock);
    if (runtime::g_graphicsStage != runtime::StageState::pending) {
        const bool active = runtime::g_graphicsStage == runtime::StageState::active;
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return active;
    }
    if (!hooks::graphics::install()) {
        runtime::g_graphicsStage = runtime::StageState::failed;
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         "ev=activate stage=graphics_hooks result=fail");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return false;
    }

    runtime::g_graphicsStage = runtime::StageState::active;
    // a disabled overlay owns neither the game window nor input/cursor guards, and no renderer
    // callback will run to retire a cursor guard
    if (!core::ui::runtime::snapshot().enabled) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
            "ev=activate stage=graphics result=ok overlay=disabled window_callbacks=0 input_guards=0");
        ReleaseSRWLockExclusive(&runtime::g_lock);
        return true;
    }
    // the game scans GetKeyState every frame, which no window procedure sees; resolve native
    // pause before selecting the required cursor guards
    (void)hooks::polled_input::install();
    // cursor guards only matter once the interface can be shown; a miss must not demote
    // presentation readiness, so log it and don't propagate
    (void)hooks::cursor::install();
    core::log::write(
        core::log::Channel::client, core::log::Level::info, "ev=activate stage=graphics result=ok");
    ReleaseSRWLockExclusive(&runtime::g_lock);
    return true;
}

} // namespace dawn::client
