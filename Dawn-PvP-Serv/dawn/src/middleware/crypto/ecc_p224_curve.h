#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::crypto::ecc::curve {

// 224 bits is 7 words of 32, least significant word first
inline constexpr std::size_t kWords = 7;
using Field = std::array<std::uint32_t, kWords>;

// infinity has no affine form; it is reported by a return value
struct Point {
    Field x{};
    Field y{};
};

[[nodiscard]] Point generator() noexcept;

// a peer key off the curve leaks the scalar, so an unchecked point is a key disclosure
[[nodiscard]] bool on_curve(const Point& point) noexcept;

[[nodiscard]] bool valid_scalar(const Field& scalar) noexcept;

[[nodiscard]] bool multiply(const Field& scalar, const Point& point, Point& output) noexcept;

void load(std::span<const std::byte> bytes, Field& output) noexcept;

void store(const Field& value, std::span<std::byte> bytes) noexcept;

} // namespace dawn::middleware::crypto::ecc::curve
