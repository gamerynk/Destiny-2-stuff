#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace dawn::middleware::crypto::ecc {

inline constexpr std::size_t kFieldSize = 28;
// exported key travels in a fixed field, zero padded past the encoding
inline constexpr std::size_t kExportedKeySize = 100;

struct Agreement {
    std::array<std::byte, kExportedKeySize> publicKey{};
    std::array<std::byte, kFieldSize> sharedSecret{};
};

// the key pair is ephemeral and destroyed before this returns
[[nodiscard]] bool agree(std::span<const std::byte> peerPublicKey, Agreement& output) noexcept;

} // namespace dawn::middleware::crypto::ecc
