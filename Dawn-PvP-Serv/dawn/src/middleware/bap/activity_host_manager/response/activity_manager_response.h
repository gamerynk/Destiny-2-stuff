#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_host_manager::response {

// the zero activity-data tail is required; its protocol meaning is unknown
[[nodiscard]] bool encode_response(std::uint64_t sessionId,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::activity_host_manager::response
