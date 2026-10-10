#pragma once

#include "../../../../../state/account/settings/settings_state.h"
#include "layout.h"

namespace dawn::middleware::datagen::family4::account::preferences {

[[nodiscard]] bool encode(const state::account::settings::AccountSettings& settings,
                          Record& record,
                          BindingsRecord& bindingsRecord) noexcept;

} // namespace dawn::middleware::datagen::family4::account::preferences
