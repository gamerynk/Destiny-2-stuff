#pragma once
#include "settings_state.h"
namespace dawn::state::account::settings {
[[nodiscard]] bool save(std::uint64_t accountSoid, const AccountSettings& before,
                        const AccountSettings& after, bool& changed) noexcept;
}
