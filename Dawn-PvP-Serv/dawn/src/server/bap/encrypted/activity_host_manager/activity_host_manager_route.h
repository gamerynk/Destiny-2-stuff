#pragma once

#include <cstddef>
#include <span>

#include "../../../../state/activity/definition.h"

namespace dawn::server::bap::encrypted::activity_host_manager {

[[nodiscard]] bool encode_response(std::span<const std::byte> requestBody,
                                   std::span<std::byte> output,
                                   std::size_t& written,
                                   state::activity::PendingAllocation& allocation,
                                   bool& hasAllocation) noexcept;

} // namespace dawn::server::bap::encrypted::activity_host_manager
