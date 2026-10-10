#pragma once

#include <string_view>

#include "../../state/account/account_state.h"
#include "../../state/activity/defaults/definition.h"
#include "../../state/investment/investment.h"
#include "../../state/unlocks/definition.h"
#include "../logging/log.h"
#include "client/definition.h"
#include "server/definition.h"
#include "steam/definition.h"

namespace dawn::core::settings {

// bump when a key is renamed, removed, changes meaning or needs a new default;
// adding a key needs no bump since a missing key takes its default
inline constexpr std::uint32_t kSettingsVersion = 7;

struct Settings {
    std::uint32_t version{};
    log::Settings logging;
    client::Settings client;
    server::Settings server;
    steam::Settings steam;
    state::AccountState initialAccount;
    state::activity::defaults::ActivityDefaults initialActivityDefaults;
    state::unlocks::Table initialUnlocks;
    state::Family5State initialFamily5;
};

[[nodiscard]] Settings defaults() noexcept;

[[nodiscard]] bool parse(std::string_view json, Settings& output) noexcept;

[[nodiscard]] bool initialize(void* module) noexcept;

void shutdown() noexcept;

[[nodiscard]] const Settings& get() noexcept;

} // namespace dawn::core::settings
