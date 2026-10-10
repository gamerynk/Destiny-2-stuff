#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::client_keepalive {

inline constexpr std::uint32_t kMessageType = 16;
inline constexpr std::size_t kEncodedSize = 1;

[[nodiscard]] bool validate_client_keepalive(std::span<const std::byte> input) noexcept;

} // namespace dawn::middleware::bap::activity_message::client_keepalive
