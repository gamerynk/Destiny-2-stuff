#include "protobuf_wire.h"

#include <climits>

namespace dawn::middleware::protobuf {
namespace {

constexpr unsigned int kWireTypeBits = 3;
constexpr std::uint64_t kWireTypeMask = 0x07;
constexpr std::uint32_t kInvalidFieldNumber = 0;
constexpr std::uint64_t kMaximumFieldNumber = 0x1FFFFFFF;
constexpr std::uint8_t kVarintPayloadMask = 0x7F;
constexpr unsigned int kVarintPayloadBits = 7;
constexpr std::uint8_t kVarintContinuationMask = 0x80;
constexpr std::size_t kMaximumVarintSize = 10;
constexpr std::uint8_t kMaximumFinalVarintByte = 0x01;
constexpr unsigned int kBitsPerByte = 8;
static_assert(CHAR_BIT == kBitsPerByte);

[[nodiscard]] constexpr bool valid_field_number(std::uint64_t fieldNumber) noexcept {
    return fieldNumber != kInvalidFieldNumber && fieldNumber <= kMaximumFieldNumber;
}

} // namespace

bool decode_key(std::uint64_t encodedKey, DecodedKey& key) noexcept {
    const std::uint64_t fieldNumber = encodedKey >> kWireTypeBits;
    if (!valid_field_number(fieldNumber)) {
        return false;
    }

    WireType wireType{};
    switch (static_cast<std::uint8_t>(encodedKey & kWireTypeMask)) {
    case static_cast<std::uint8_t>(WireType::varint):
        wireType = WireType::varint;
        break;
    case static_cast<std::uint8_t>(WireType::fixed64):
        wireType = WireType::fixed64;
        break;
    case static_cast<std::uint8_t>(WireType::lengthDelimited):
        wireType = WireType::lengthDelimited;
        break;
    case static_cast<std::uint8_t>(WireType::fixed32):
        wireType = WireType::fixed32;
        break;
    default:
        return false;
    }

    key.fieldNumber = static_cast<std::uint32_t>(fieldNumber);
    key.wireType = wireType;
    return true;
}

bool encode_key(std::uint32_t fieldNumber, WireType wireType, std::uint64_t& encodedKey) noexcept {
    if (!valid_field_number(fieldNumber)) {
        return false;
    }
    encodedKey = (static_cast<std::uint64_t>(fieldNumber) << kWireTypeBits)
                 | static_cast<std::uint8_t>(wireType);
    return true;
}

bool read_varint(std::span<const std::byte> input,
                 std::size_t& cursor,
                 std::uint64_t& value) noexcept {
    value = 0;
    for (std::size_t index = 0; index < kMaximumVarintSize; ++index) {
        if (cursor == input.size()) {
            return false;
        }
        const auto byte = std::to_integer<std::uint8_t>(input[cursor]);
        if (index == kMaximumVarintSize - 1 && byte > kMaximumFinalVarintByte) {
            return false;
        }
        value |= static_cast<std::uint64_t>(byte & kVarintPayloadMask)
                 << (index * kVarintPayloadBits);
        ++cursor;
        if ((byte & kVarintContinuationMask) == 0) {
            return true;
        }
    }
    return false;
}

std::uint64_t read_fixed(std::span<const std::byte> input) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        value |= std::to_integer<std::uint64_t>(input[index]) << (index * kBitsPerByte);
    }
    return value;
}

std::size_t varint_size(std::uint64_t value) noexcept {
    std::size_t size = 1;
    while (value > kVarintPayloadMask) {
        value >>= kVarintPayloadBits;
        ++size;
    }
    return size;
}

std::size_t write_raw_varint(std::uint64_t value, std::span<std::byte> output) noexcept {
    std::size_t written = 0;
    while (value > kVarintPayloadMask) {
        output[written++] =
            static_cast<std::byte>((value & kVarintPayloadMask) | kVarintContinuationMask);
        value >>= kVarintPayloadBits;
    }
    output[written++] = static_cast<std::byte>(value);
    return written;
}

} // namespace dawn::middleware::protobuf
