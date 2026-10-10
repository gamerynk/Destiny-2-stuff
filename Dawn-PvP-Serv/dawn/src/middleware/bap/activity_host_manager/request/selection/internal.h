#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../../../encoding/bit_reader.h"
#include "definition.h"

namespace dawn::middleware::bap::activity_host_manager::request::selection {

[[nodiscard]] bool parse_field_one(std::span<const std::byte> input,
                                   ActivityManagerSelection& selection) noexcept;

[[nodiscard]] bool parse_field_two(std::span<const std::byte> input,
                                   ActivityManagerSelection& selection) noexcept;

[[nodiscard]] bool copy_bits(std::span<const std::byte> source,
                             std::size_t startBit,
                             std::size_t bitCount,
                             std::span<std::byte> output,
                             std::size_t& outputBitLength) noexcept;

[[nodiscard]] bool read_presence(encoding::bits::Reader& reader, bool& present) noexcept;

[[nodiscard]] bool read_i8(encoding::bits::Reader& reader,
                           std::uint8_t width,
                           std::int16_t bias,
                           std::int8_t& value) noexcept;

[[nodiscard]] bool read_i16(encoding::bits::Reader& reader,
                            std::uint8_t width,
                            std::int16_t bias,
                            std::int16_t& value) noexcept;

[[nodiscard]] bool skip_optional(encoding::bits::Reader& reader, std::size_t width) noexcept;

[[nodiscard]] bool
read_hash(encoding::bits::Reader& reader, bool& present, std::uint32_t& value) noexcept;

[[nodiscard]] bool parse(encoding::bits::Reader& reader,
                         ActivityManagerSelection& selection) noexcept;

} // namespace dawn::middleware::bap::activity_host_manager::request::selection
