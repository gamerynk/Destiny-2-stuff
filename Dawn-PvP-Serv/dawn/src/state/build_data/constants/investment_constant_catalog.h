#pragma once

#include "definition.h"

namespace dawn::state::build_data::constants {

void clear() noexcept;

[[nodiscard]] bool replace(const InvestmentConstants& value) noexcept;

[[nodiscard]] bool find(InvestmentConstants& value) noexcept;

[[nodiscard]] InvestmentConstants snapshot() noexcept;

} // namespace dawn::state::build_data::constants
