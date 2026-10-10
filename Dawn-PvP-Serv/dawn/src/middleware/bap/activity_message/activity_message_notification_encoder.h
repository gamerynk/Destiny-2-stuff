#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace dawn::middleware::bap::activity_message {

// the client's notification router accepts message types 0 through 58
inline constexpr std::uint32_t kMaximumMessageType = 58;

[[nodiscard]] bool encode_notification(std::uint64_t sessionId,
                                       std::uint32_t messageType,
                                       std::span<const std::byte> payload,
                                       std::span<std::byte> output,
                                       std::size_t& written) noexcept;

} // namespace dawn::middleware::bap::activity_message
