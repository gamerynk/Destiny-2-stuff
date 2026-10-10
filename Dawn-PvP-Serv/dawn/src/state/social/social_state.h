#pragma once

#include "definition.h"

namespace dawn::state::social {

[[nodiscard]] bool publish(const Snapshot& snapshot) noexcept;

[[nodiscard]] Snapshot snapshot() noexcept;

void clear() noexcept;

} // namespace dawn::state::social
