#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::high_water {

inline constexpr std::uint32_t kMessageType = 49;
inline constexpr std::size_t kEncodedSize = 52;

[[nodiscard]] bool validate_high_water(std::span<const std::byte> input) noexcept;

} // namespace dawn::middleware::bap::activity_message::high_water
