#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace dawn::middleware::crypto::sha256 {

inline constexpr std::size_t kDigestSize = 32;

using Digest = std::array<std::byte, kDigestSize>;

[[nodiscard]] bool hash(std::span<const std::byte> input, Digest& output) noexcept;

// the SRP scrambling parameter is defined over a concatenation, so no scratch copy
[[nodiscard]] bool hash_pair(std::span<const std::byte> first,
                             std::span<const std::byte> second,
                             Digest& output) noexcept;

} // namespace dawn::middleware::crypto::sha256
