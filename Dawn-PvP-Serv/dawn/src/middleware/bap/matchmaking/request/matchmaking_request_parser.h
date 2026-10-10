#pragma once

#include <cstddef>
#include <span>

#include "../definition.h"

namespace dawn::middleware::bap::matchmaking::request {

[[nodiscard]] Request parse(std::span<const std::byte> input) noexcept;

} // namespace dawn::middleware::bap::matchmaking::request
