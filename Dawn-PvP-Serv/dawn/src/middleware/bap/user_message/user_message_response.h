#pragma once

#include <cstddef>
#include <span>

namespace dawn::middleware::bap::user_message {

[[nodiscard]] bool encode_minimal_response(std::span<std::byte> output,
                                           std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::user_message
