#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace dawn::middleware::gameplay::dtls {

// the join key the descriptor advertises
inline constexpr std::size_t kSecurityKeySize = 16;
// shared secret, security key, then a zero-padded suffix
inline constexpr std::size_t kDerivationSize = 68;
// the digest is truncated to this
inline constexpr std::size_t kCypherKeySize = 16;
inline constexpr std::size_t kAuthKeySize = 24;

struct Keys {
    std::array<std::byte, kCypherKeySize> cypher{};
    std::array<std::byte, kAuthKeySize> auth{};
};

// agreed x coordinate, high byte first
[[nodiscard]] bool derive(std::span<const std::byte> sharedSecret,
                          std::span<const std::byte> securityKey,
                          Keys& output) noexcept;

} // namespace dawn::middleware::gameplay::dtls
