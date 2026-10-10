#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace dawn::middleware::bap::activity_message::join_request {

[[nodiscard]] bool parse_join_request(std::span<const std::byte> input,
                                      JoinRequest& request) noexcept;

} // namespace dawn::middleware::bap::activity_message::join_request
