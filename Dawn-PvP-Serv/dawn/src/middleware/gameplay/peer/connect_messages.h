#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"
#include "../descriptor/join_descriptor.h"

namespace dawn::middleware::gameplay::peer {

inline constexpr std::size_t kAddressBlobSize = descriptor::kNetAddrSize;

enum class ConnectId : std::uint8_t {
    request = 5,
    response = 6,
    refuse = 7,
    establish = 8,
    closed = 9,
};

inline constexpr std::uint32_t kRequestSize = 96;
inline constexpr std::uint32_t kResponseSize = 104;
inline constexpr std::uint32_t kRefuseSize = 12;
inline constexpr std::uint32_t kEstablishSize = 8;
inline constexpr std::uint32_t kClosedSize = 12;

struct ConnectRequest {
    // ordered, not random, and never all ones
    std::uint32_t channelId{};
    std::uint32_t sequence{};
    std::array<std::byte, kAddressBlobSize> address{};
};

struct ConnectResponse {
    std::uint32_t remoteChannelId{};
    std::uint32_t remoteSequence{};
    std::uint32_t channelId{};
    std::uint32_t sequence{};
    std::array<std::byte, kAddressBlobSize> address{};
};

struct ConnectEstablish {
    std::uint32_t remoteChannelId{};
    std::uint32_t channelId{};
};

struct ConnectEnd {
    std::uint32_t remoteChannelId{};
    std::uint32_t remoteSequence{};
    std::uint8_t reason{};
};

[[nodiscard]] bool read_request(encoding::bits::Reader& reader, ConnectRequest& output) noexcept;

[[nodiscard]] bool write_response(encoding::bits::Writer& writer,
                                  const ConnectResponse& body) noexcept;

[[nodiscard]] bool read_establish(encoding::bits::Reader& reader,
                                  ConnectEstablish& output) noexcept;

[[nodiscard]] bool write_establish(encoding::bits::Writer& writer,
                                   const ConnectEstablish& body) noexcept;

[[nodiscard]] bool write_refuse(encoding::bits::Writer& writer, const ConnectEnd& body) noexcept;

[[nodiscard]] bool write_closed(encoding::bits::Writer& writer, const ConnectEnd& body) noexcept;

[[nodiscard]] bool read_closed(encoding::bits::Reader& reader, ConnectEnd& output) noexcept;

} // namespace dawn::middleware::gameplay::peer
