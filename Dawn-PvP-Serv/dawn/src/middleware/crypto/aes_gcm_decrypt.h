#pragma once

#include <cstddef>
#include <span>

namespace dawn::middleware::crypto::aes_gcm {

// package and channel material is AES-128
inline constexpr std::size_t kKeySize = 16;
inline constexpr std::size_t kNonceSize = 12;
inline constexpr std::size_t kTagSize = 16;

[[nodiscard]] bool decrypt(std::span<const std::byte, kKeySize> key,
                           std::span<const std::byte, kNonceSize> nonce,
                           std::span<const std::byte> ciphertext,
                           std::span<const std::byte, kTagSize> tag,
                           std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::crypto::aes_gcm
