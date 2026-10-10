#pragma once

#include <cstdint>

#include "../account/account_state.h"

namespace dawn::state::social::remote_profile_cache {

[[nodiscard]] bool resolve(std::uint64_t accountSoid, AccountState& output) noexcept;

void clear() noexcept;

} // namespace dawn::state::social::remote_profile_cache
