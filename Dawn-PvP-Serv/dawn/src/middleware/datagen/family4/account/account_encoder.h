#pragma once

#include <span>

#include "../../../../state/account/account_state.h"

namespace dawn::middleware::datagen::family4::account {

[[nodiscard]] bool encode(const state::AccountState& state, std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::datagen::family4::account
