#pragma once

#include <cstddef>
#include <span>

namespace dawn::middleware::crypto::random {

// every ephemeral secret comes from here, never from an authored default
[[nodiscard]] bool fill(std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::crypto::random
