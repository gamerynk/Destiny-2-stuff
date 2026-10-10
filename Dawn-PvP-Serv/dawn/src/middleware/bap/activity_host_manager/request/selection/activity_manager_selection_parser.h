#pragma once

#include <cstddef>
#include <span>

#include "definition.h"

namespace dawn::middleware::bap::activity_host_manager::request::selection {

[[nodiscard]] bool parse_selection(std::span<const std::byte> input,
                                   ActivityManagerSelectionResult& result) noexcept;

} // namespace dawn::middleware::bap::activity_host_manager::request::selection
