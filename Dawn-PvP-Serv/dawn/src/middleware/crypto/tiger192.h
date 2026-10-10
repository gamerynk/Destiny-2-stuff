#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace dawn::middleware::crypto::tiger {

inline constexpr std::size_t kDigestSize = 24;

using Digest = std::array<std::byte, kDigestSize>;

// Windows has no provider for Tiger, so it is implemented here
void hash(std::span<const std::byte> input, Digest& output) noexcept;

} // namespace dawn::middleware::crypto::tiger
