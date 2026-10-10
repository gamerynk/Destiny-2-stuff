#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::crypto::lookup3 {

// lookup3 word form; the start value is a caller literal, not the usual length and seed
[[nodiscard]] std::uint32_t hash_words(std::span<const std::uint32_t> words,
                                       std::uint32_t initial) noexcept;

[[nodiscard]] std::uint32_t hash_bytes(std::span<const std::byte> bytes,
                                       std::uint32_t initial) noexcept;

} // namespace dawn::middleware::crypto::lookup3
