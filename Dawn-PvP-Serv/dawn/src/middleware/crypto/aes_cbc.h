#pragma once

#include <cstddef>
#include <span>

namespace dawn::middleware::crypto::aes {

inline constexpr std::size_t kBlockSize = 16;

// no padding; length must already be whole blocks. output may alias input
[[nodiscard]] bool encrypt(std::span<const std::byte> key,
                           std::span<const std::byte> iv,
                           std::span<const std::byte> input,
                           std::span<std::byte> output) noexcept;

[[nodiscard]] bool decrypt(std::span<const std::byte> key,
                           std::span<const std::byte> iv,
                           std::span<const std::byte> input,
                           std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::crypto::aes
