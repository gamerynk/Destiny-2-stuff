#pragma once

#include <array>
#include <cstddef>

#include "../../crypto/modular_1024.h"
#include "../../crypto/sha256.h"

namespace dawn::middleware::gameplay::association::srp {

// every exchanged integer is 128 bytes, big-endian, left zero padded
inline constexpr std::size_t kIntegerSize = crypto::modular::kByteSize;
inline constexpr std::size_t kDerivedKeySize = crypto::sha256::kDigestSize;

using Integer = std::array<std::byte, kIntegerSize>;

struct Exchange {
    Integer verifier{};
    Integer clientPublic{};
    Integer serverPublic{};
    // SHA-256 of the shared value; its first 16 bytes key the protected envelope
    std::array<std::byte, kDerivedKeySize> derivedKey{};
    bool complete{};
};

// the private exponent is erased before return; a public value of 0 or >= modulus is refused (our policy)
[[nodiscard]] bool derive(Exchange& exchange) noexcept;

} // namespace dawn::middleware::gameplay::association::srp
