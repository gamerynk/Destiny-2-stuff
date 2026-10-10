#pragma once

#include <cstddef>

#include "../../../account/account_state.h"
#include "../definition.h"

namespace dawn::state::equipment::light::resolution {

[[nodiscard]] bool resolve(const AccountState& account,
                           std::size_t selectedCharacterIndex,
                           Evaluation& output) noexcept;

[[nodiscard]] bool character_light(const AccountState& account,
                                   std::size_t characterIndex,
                                   std::int32_t& light) noexcept;

} // namespace dawn::state::equipment::light::resolution
