#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::crypto::hmac {

inline constexpr std::size_t kMaximumDigestSize = 32;

struct Digest {
    std::array<std::byte, kMaximumDigestSize> bytes{};
    std::size_t size{};
};

// numbered with the ids the peer picks them by
enum class Algorithm : std::uint8_t {
    sha256 = 0,
    sha1 = 2,
    murmur3 = 4,
};

// two buffers because the record skips the bytes between its header and body
[[nodiscard]] bool authenticate(Algorithm algorithm,
                                std::span<const std::byte> key,
                                std::span<const std::byte> first,
                                std::span<const std::byte> second,
                                Digest& output) noexcept;

} // namespace dawn::middleware::crypto::hmac
