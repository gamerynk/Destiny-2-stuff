#pragma once

#include <limits>

#include "codec.h"

namespace dawn::middleware::protobuf {

static_assert((std::numeric_limits<std::size_t>::max)()
              <= (std::numeric_limits<std::uint64_t>::max)());

struct DecodedKey final {
    std::uint32_t fieldNumber{};
    WireType wireType{WireType::varint};
};

[[nodiscard]] bool decode_key(std::uint64_t encodedKey, DecodedKey& key) noexcept;

[[nodiscard]] bool
encode_key(std::uint32_t fieldNumber, WireType wireType, std::uint64_t& encodedKey) noexcept;

[[nodiscard]] bool
read_varint(std::span<const std::byte> input, std::size_t& cursor, std::uint64_t& value) noexcept;

[[nodiscard]] std::uint64_t read_fixed(std::span<const std::byte> input) noexcept;

[[nodiscard]] std::size_t varint_size(std::uint64_t value) noexcept;

std::size_t write_raw_varint(std::uint64_t value, std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::protobuf
