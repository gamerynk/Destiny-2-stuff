#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace dawn::middleware::bap::activity_message {

[[nodiscard]] bool parse_request(std::span<const std::byte> input, Request& request) noexcept;

} // namespace dawn::middleware::bap::activity_message
