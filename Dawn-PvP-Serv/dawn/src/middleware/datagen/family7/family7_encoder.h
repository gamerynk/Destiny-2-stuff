#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::datagen::family7 {

inline constexpr std::size_t kAnchorSize = 0x10;
inline constexpr std::size_t kBodySize = 0x98;
inline constexpr std::size_t kJoinDescriptorSize = 0x80;

// live getter observer captured 0x5C01 on every call from the unmodified client join path;
// advertising 0x5C00 sets exactly the native different-version bit (0x00200000)
inline constexpr std::uint64_t kCompatibilitySignature = 0x5C01U;

[[nodiscard]] bool encode_anchor(std::uint64_t accountSoid,
                                 std::uint64_t bodySoid,
                                 std::span<std::byte> destination) noexcept;

[[nodiscard]] bool encode_body(std::uint64_t bodySoid,
                               std::span<const std::byte> joinDescriptor,
                               std::uint64_t compatibilitySignature,
                               std::span<std::byte> destination) noexcept;

} // namespace dawn::middleware::datagen::family7
