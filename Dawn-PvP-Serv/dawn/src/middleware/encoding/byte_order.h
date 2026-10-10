#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::encoding {

inline constexpr std::size_t kU16Size = sizeof(std::uint16_t);
inline constexpr std::size_t kU32Size = sizeof(std::uint32_t);
inline constexpr std::size_t kU64Size = sizeof(std::uint64_t);
inline constexpr std::size_t kBitsPerByte = 8;
inline constexpr std::size_t kU16HighByteShift = (kU16Size - 1) * kBitsPerByte;
inline constexpr std::size_t kU32ByteZeroShift = (kU32Size - 1) * kBitsPerByte;
inline constexpr std::size_t kU32ByteOneShift = kU32ByteZeroShift - kBitsPerByte;
inline constexpr std::size_t kU32ByteTwoShift = kU32ByteOneShift - kBitsPerByte;

[[nodiscard]] inline std::uint16_t
read_u16_be(std::span<const std::byte, kU16Size> input) noexcept {
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(input[0]) << kU16HighByteShift)
        | std::to_integer<std::uint16_t>(input[1]));
}

[[nodiscard]] inline std::uint32_t
read_u32_be(std::span<const std::byte, kU32Size> input) noexcept {
    return (std::to_integer<std::uint32_t>(input[0]) << kU32ByteZeroShift)
           | (std::to_integer<std::uint32_t>(input[1]) << kU32ByteOneShift)
           | (std::to_integer<std::uint32_t>(input[2]) << kU32ByteTwoShift)
           | std::to_integer<std::uint32_t>(input[3]);
}

[[nodiscard]] inline std::uint32_t
read_u32_le(std::span<const std::byte, kU32Size> input) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        value |= std::to_integer<std::uint32_t>(input[index]) << (index * kBitsPerByte);
    }
    return value;
}

[[nodiscard]] inline std::uint64_t
read_u64_be(std::span<const std::byte, kU64Size> input) noexcept {
    std::uint64_t value = 0;
    for (const std::byte byte : input) {
        value = (value << kBitsPerByte) | std::to_integer<std::uint64_t>(byte);
    }
    return value;
}

[[nodiscard]] inline std::uint64_t
read_u64_le(std::span<const std::byte, kU64Size> input) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        value |= std::to_integer<std::uint64_t>(input[index]) << (index * kBitsPerByte);
    }
    return value;
}

inline void write_u16_be(std::span<std::byte, kU16Size> output, std::uint16_t value) noexcept {
    output[0] = static_cast<std::byte>(value >> kU16HighByteShift);
    output[1] = static_cast<std::byte>(value);
}

inline void write_u32_be(std::span<std::byte, kU32Size> output, std::uint32_t value) noexcept {
    output[0] = static_cast<std::byte>(value >> kU32ByteZeroShift);
    output[1] = static_cast<std::byte>(value >> kU32ByteOneShift);
    output[2] = static_cast<std::byte>(value >> kU32ByteTwoShift);
    output[3] = static_cast<std::byte>(value);
}

inline void write_u64_be(std::span<std::byte, kU64Size> output, std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < output.size(); ++index) {
        const std::size_t shift = (output.size() - index - 1) * kBitsPerByte;
        output[index] = static_cast<std::byte>(value >> shift);
    }
}

} // namespace dawn::middleware::encoding
