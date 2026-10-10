#pragma once

#include "definition.h"

namespace dawn::state::unlocks {

void publish(const Table& table) noexcept;

[[nodiscard]] const Table& get() noexcept;

void clear() noexcept;

} // namespace dawn::state::unlocks
