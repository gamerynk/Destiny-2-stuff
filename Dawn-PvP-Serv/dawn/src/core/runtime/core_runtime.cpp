#include "core_runtime.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../client/runtime/host/game_host_classification.h"
#include "../../client/runtime/runtime.h"
#include "../../steam/runtime/native_network_diagnostics.h"
#include "../../middleware/runtime/middleware_runtime.h"
#include "../../server/runtime/server_runtime.h"
#include "../../state/content_manifest/content_manifest_state_runtime.h"
#include "../../state/entitlements/entitlement_runtime.h"
#include "../../state/runtime/equipment/configured_equipment_identity.h"
#include "../../state/runtime/runtime.h"
#include "../../state/unlocks/unlocks_runtime.h"
#include "../filesystem/path.h"
#include "../logging/log.h"
#include "../settings/settings.h"
#include "../ui/modules/hud/hud.h"
#include "../ui/modules/logs/logs.h"
#include "../ui/modules/registry/ui_module_registry.h"
#include "../ui/runtime/ui_visibility_runtime.h"
#include "runtime_policy.h"

namespace dawn::core {
namespace {

std::atomic_bool g_initialized{false};
SRWLOCK g_runtimeLock{SRWLOCK_INIT};

constexpr std::wstring_view kInstalledPackagesDirectory = L"packages";

[[nodiscard]] bool initialize_content_manifest(void* module) noexcept {
    if (client::runtime::host::current_requirement()
        == client::runtime::host::NetworkRequirement::notApplicable) {
        return true;
    }
    const HMODULE process = GetModuleHandleW(nullptr);
    path::Buffer packages;
    if (process == nullptr || !path::module_directory(process, packages)
        || !path::append(packages, kInstalledPackagesDirectory)) {
        log::write(log::Channel::core,
                   log::Level::error,
                   "ev=initialize stage=content_manifest result=fail");
        return false;
    }
    const bool initialized = state::content_manifest::initialize(
        module, std::wstring_view(packages.chars.data(), packages.length));
    if (!initialized) {
        log::write(log::Channel::core,
                   log::Level::error,
                   "ev=initialize stage=content_manifest result=fail");
    }
    return initialized;
}

// distribution clients start with an empty local account until server snapshots arrive;
// the equipment fingerprint is kept only to reuse the build-data cache
[[nodiscard]] bool initialize_state(void* module) noexcept {
#if defined(RESTORATION_UNIVERSAL_CLIENT) && RESTORATION_UNIVERSAL_CLIENT
    state::AccountRuntimeOptions options{};
    options.buildDataEquipmentHash =
        state::runtime::equipment::configured_hash(settings::get().initialAccount);
    options.hasBuildDataEquipmentHash = true;
    return state::initialize(
        module, state::AccountState{}, settings::get().initialActivityDefaults, options);
#else
    return state::initialize(module,
                             settings::get().initialAccount,
                             settings::get().initialActivityDefaults);
#endif
}

void report_stage_failure(const char* stage) noexcept {
    std::array<char, 96> line{};
    const int written =
        std::snprintf(line.data(), line.size(), "ev=initialize stage=%s result=fail", stage);
    if (written <= 0) {
        return;
    }
    const std::string_view event{line.data(), static_cast<std::size_t>(written)};
    log::early(event);
    log::write(log::Channel::core, log::Level::error, event);
}

} // namespace

bool initialize(void* module) noexcept {
    AcquireSRWLockExclusive(&g_runtimeLock);
    if (g_initialized.load(std::memory_order_relaxed)) {
        ReleaseSRWLockExclusive(&g_runtimeLock);
        return true;
    }
    const std::uint64_t startedTick = GetTickCount64();

    if (!settings::initialize(module)) {
        ReleaseSRWLockExclusive(&g_runtimeLock);
        return false;
    }
    if (runtime::policy::kDedicatedBackendRequired
        && !settings::get().client.externalServer.enabled) {
        report_stage_failure("dedicated_backend_policy");
        settings::shutdown();
        ReleaseSRWLockExclusive(&g_runtimeLock);
        return false;
    }
    state::unlocks::publish(settings::get().initialUnlocks);
    const char* stage = nullptr;
    if (!log::initialize(module, settings::get().logging)) {
        stage = "logging";
    } else {
        log::write(log::Channel::core, log::Level::debug, "ev=initialize phase=begin");
        runtime::policy::report();
        if (!ui::runtime::initialize(settings::get().client.userInterface)) {
            stage = "ui";
        } else if (!ui::modules::hud::initialize(module)) {
            stage = "ui_hud";
        } else if (!ui::modules::logs::initialize()) {
            stage = "ui_logs";
        } else if (!state::entitlements::publish(settings::get().server.entitlements)) {
            stage = "entitlements";
        } else if (!initialize_state(module)) {
            stage = "state";
        } else if (!initialize_content_manifest(module)) {
            stage = "content_manifest";
        } else if (!middleware::initialize()) {
            stage = "middleware";
        } else {
            if (stage == nullptr && !client::initialize(module)) {
                stage = "client";
            }
        }
    }
    if (stage != nullptr) {
        report_stage_failure(stage);
        // reported before the unwind so the duration covers init only, not the quiesce waits
        log::write_elapsed(log::Channel::core, "ev=initialize phase=complete", startedTick, "fail");
        (void)client::shutdown();
        server::shutdown();
        middleware::shutdown();
        state::content_manifest::shutdown();
        state::shutdown();
        state::entitlements::clear();
        ui::modules::logs::shutdown();
        ui::modules::hud::shutdown();
        ui::modules::registry::shutdown();
        ui::runtime::shutdown();
        state::unlocks::clear();
        log::shutdown();
        settings::shutdown();
        ReleaseSRWLockExclusive(&g_runtimeLock);
        return false;
    }
    g_initialized.store(true, std::memory_order_release);
    log::write(log::Channel::core, log::Level::info, "ev=initialize result=ok");
    log::write_elapsed(log::Channel::core, "ev=initialize phase=complete", startedTick, "ok");
    ReleaseSRWLockExclusive(&g_runtimeLock);
    return true;
}

bool shutdown() noexcept {
    AcquireSRWLockExclusive(&g_runtimeLock);
    if (!g_initialized.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_runtimeLock);
        return true;
    }
    if (!client::shutdown()) {
        // Server and State must stay valid while any Client hook is attached
        ReleaseSRWLockExclusive(&g_runtimeLock);
        return false;
    }
    steam::runtime::native_network_diagnostics::shutdown();
    g_initialized.store(false, std::memory_order_release);
    server::shutdown();
    middleware::shutdown();
    state::content_manifest::shutdown();
    state::shutdown();
    state::entitlements::clear();
    ui::modules::logs::shutdown();
    ui::modules::hud::shutdown();
    ui::modules::registry::shutdown();
    ui::runtime::shutdown();
    log::write(log::Channel::core, log::Level::info, "ev=shutdown result=ok");
    state::unlocks::clear();
    log::shutdown();
    settings::shutdown();
    ReleaseSRWLockExclusive(&g_runtimeLock);
    return true;
}

bool is_initialized() noexcept {
    return g_initialized.load(std::memory_order_acquire);
}

} // namespace dawn::core
