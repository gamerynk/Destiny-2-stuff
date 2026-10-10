#pragma once

#include <cstddef>
#include <span>

#include "../definition.h"

namespace dawn::middleware::bap::matchmaking::response {

[[nodiscard]] bool
encode(const Response& response, std::span<std::byte> output, std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::matchmaking::response
