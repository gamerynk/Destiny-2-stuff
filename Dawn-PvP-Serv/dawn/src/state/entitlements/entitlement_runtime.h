#pragma once

#include "definition.h"

namespace dawn::state::entitlements {

[[nodiscard]] bool publish(const Table& table) noexcept;

[[nodiscard]] const Table& get() noexcept;

void clear() noexcept;

} // namespace dawn::state::entitlements
