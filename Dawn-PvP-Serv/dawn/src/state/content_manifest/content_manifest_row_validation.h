#pragma once

#include <span>

#include "definition.h"

namespace dawn::state::content_manifest {

[[nodiscard]] bool valid(const Row& row) noexcept;

[[nodiscard]] bool valid(std::span<const Row> rows) noexcept;

} // namespace dawn::state::content_manifest
