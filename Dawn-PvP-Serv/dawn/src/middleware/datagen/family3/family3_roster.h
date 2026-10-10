#pragma once

#include <cstddef>
#include <span>

#include "../../../state/account/account_state.h"

namespace dawn::middleware::datagen::family3 {

inline constexpr std::size_t kRosterSize = 1768;

[[nodiscard]] bool encode_roster(const state::AccountState& account,
                                 std::span<std::byte> output,
                                 std::size_t& written) noexcept;

} // namespace dawn::middleware::datagen::family3
