#pragma once

#include <cstddef>
#include <span>

#include "bit_reader.h"
#include "bit_writer.h"

namespace dawn::middleware::encoding::bits {

// raw fields copy bytes in engine order; value fields are MSB first
[[nodiscard]] bool write_raw(Writer& writer, std::span<const std::byte> bytes) noexcept;

[[nodiscard]] bool read_raw(Reader& reader, std::span<std::byte> bytes) noexcept;

[[nodiscard]] bool skip_raw(Reader& reader, std::size_t count) noexcept;

// raw keeps memory order, so an engine integer arrives low byte first
[[nodiscard]] bool read_raw_u64(Reader& reader, std::uint64_t& value) noexcept;

[[nodiscard]] bool write_raw_u64(Writer& writer, std::uint64_t value) noexcept;

} // namespace dawn::middleware::encoding::bits
