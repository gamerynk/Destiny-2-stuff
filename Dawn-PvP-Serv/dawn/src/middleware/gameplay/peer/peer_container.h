#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace dawn::middleware::gameplay::peer {

inline constexpr std::uint8_t kMaximumMessageId = 44;
inline constexpr std::size_t kContainerCapacity = 5120;

struct MessageHeader {
    std::uint8_t id{};
    std::uint32_t declaredSize{};
};

[[nodiscard]] bool open_container(encoding::bits::Writer& writer) noexcept;

[[nodiscard]] bool write_header(encoding::bits::Writer& writer,
                                const MessageHeader& header) noexcept;

[[nodiscard]] bool close_container(encoding::bits::Writer& writer) noexcept;

[[nodiscard]] bool read_marker(encoding::bits::Reader& reader) noexcept;

[[nodiscard]] bool
read_header(encoding::bits::Reader& reader, MessageHeader& header, bool& present) noexcept;

} // namespace dawn::middleware::gameplay::peer
