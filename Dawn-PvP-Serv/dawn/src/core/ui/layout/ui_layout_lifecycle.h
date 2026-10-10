#pragma once

#include <string_view>

#include "layout.h"

namespace dawn::core::ui::layout::internal {

[[nodiscard]] bool context_is_current() noexcept;

void select_module(std::string_view stableId) noexcept;

} // namespace dawn::core::ui::layout::internal
