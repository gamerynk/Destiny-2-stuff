#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace dawn::middleware::crypto::murmur3 {

inline constexpr std::size_t kDigestSize = 16;

using Digest = std::array<std::byte, kDigestSize>;

// two buffers because callers hash a fixed block followed by a body
void hash(std::span<const std::byte> first,
          std::span<const std::byte> second,
          Digest& output) noexcept;

} // namespace dawn::middleware::crypto::murmur3
