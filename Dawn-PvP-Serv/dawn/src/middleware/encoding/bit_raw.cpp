#include "bit_raw.h"

#include <cstdint>

namespace dawn::middleware::encoding::bits {

namespace {

constexpr std::uint8_t kByteWidth = 8;
constexpr std::uint64_t kByteMask = 0xFF;

} // namespace

bool write_raw(Writer& writer, std::span<const std::byte> bytes) noexcept {
    for (const std::byte value : bytes) {
        if (!writer.write(std::to_integer<std::uint64_t>(value), kByteWidth)) {
            return false;
        }
    }
    return true;
}

bool read_raw(Reader& reader, std::span<std::byte> bytes) noexcept {
    for (std::byte& value : bytes) {
        std::uint64_t field = 0;
        if (!reader.read(kByteWidth, field)) {
            return false;
        }
        value = static_cast<std::byte>(field);
    }
    return true;
}

bool skip_raw(Reader& reader, std::size_t count) noexcept {
    return reader.skip(count * kByteWidth);
}

bool read_raw_u64(Reader& reader, std::uint64_t& value) noexcept {
    std::uint64_t assembled = 0;
    for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
        std::uint64_t byte = 0;
        if (!reader.read(kByteWidth, byte)) {
            return false;
        }
        assembled |= byte << (index * kByteWidth);
    }
    value = assembled;
    return true;
}

bool write_raw_u64(Writer& writer, std::uint64_t value) noexcept {
    for (std::size_t index = 0; index < sizeof(std::uint64_t); ++index) {
        if (!writer.write((value >> (index * kByteWidth)) & kByteMask, kByteWidth)) {
            return false;
        }
    }
    return true;
}

} // namespace dawn::middleware::encoding::bits
