#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::crypto::modular {

// direct-association integers are 1024 bits, sent as 128 big-endian bytes
inline constexpr std::size_t kByteSize = 128;
inline constexpr std::size_t kLimbCount = kByteSize / sizeof(std::uint64_t);

using Number = std::array<std::uint64_t, kLimbCount>;

// build once per process; preparing costs far more than a single multiply
struct Modulus {
    Number value{};
    Number montgomeryFactor{};
    std::uint64_t inverse{};
    bool ready{};
};

void import_big_endian(std::span<const std::byte, kByteSize> input, Number& output) noexcept;

void export_big_endian(const Number& value, std::span<std::byte, kByteSize> output) noexcept;

void set_small(Number& output, std::uint64_t value) noexcept;

[[nodiscard]] bool is_zero(const Number& value) noexcept;

[[nodiscard]] bool less_than(const Number& left, const Number& right) noexcept;

[[nodiscard]] bool prepare(const Number& modulus, Modulus& output) noexcept;

void add(const Modulus& modulus, const Number& left, const Number& right, Number& output) noexcept;

void multiply(const Modulus& modulus,
              const Number& left,
              const Number& right,
              Number& output) noexcept;

void power(const Modulus& modulus,
           const Number& base,
           const Number& exponent,
           Number& output) noexcept;

} // namespace dawn::middleware::crypto::modular
