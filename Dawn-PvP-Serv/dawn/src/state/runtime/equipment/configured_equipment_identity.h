#pragma once

#include <cstdint>

#include "../../account/account_state.h"

namespace dawn::state::runtime::equipment {

[[nodiscard]] std::uint64_t configured_hash(const AccountState& accountState) noexcept;

} // namespace dawn::state::runtime::equipment
