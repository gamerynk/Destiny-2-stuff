#pragma once

#include "definition.h"

namespace dawn::state::activity::defaults {

[[nodiscard]] bool valid(const DefaultDestination& candidate) noexcept;

[[nodiscard]] bool valid(const ActivityDefaults& candidate) noexcept;

[[nodiscard]] ActivityDefaults authored() noexcept;

} // namespace dawn::state::activity::defaults
