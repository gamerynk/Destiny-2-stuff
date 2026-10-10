#pragma once

#include <cstddef>
#include <span>

#include "aes_gcm_decrypt.h"

namespace dawn::middleware::crypto::aes_gcm {

// no additional authenticated data; the tag covers the ciphertext only
[[nodiscard]] bool encrypt(std::span<const std::byte, kKeySize> key,
                           std::span<const std::byte, kNonceSize> nonce,
                           std::span<const std::byte> plaintext,
                           std::span<std::byte> output,
                           std::span<std::byte, kTagSize> tag) noexcept;

} // namespace dawn::middleware::crypto::aes_gcm
