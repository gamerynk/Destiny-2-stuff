#include "connect_messages.h"

#include "../../encoding/bit_raw.h"

namespace dawn::middleware::gameplay::peer {

namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kSequenceWidth = 32;
constexpr std::uint8_t kRefuseReasonWidth = 3;
constexpr std::uint8_t kCloseReasonWidth = 5;

[[nodiscard]] bool
read_sequences(bits::Reader& reader, std::uint32_t& connection, std::uint32_t& transport) noexcept {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    if (!reader.read(kSequenceWidth, first) || !reader.read(kSequenceWidth, second)) {
        return false;
    }
    connection = static_cast<std::uint32_t>(first);
    transport = static_cast<std::uint32_t>(second);
    return true;
}

[[nodiscard]] bool
write_sequences(bits::Writer& writer, std::uint32_t connection, std::uint32_t transport) noexcept {
    return writer.write(connection, kSequenceWidth) && writer.write(transport, kSequenceWidth);
}

} // namespace

bool read_request(bits::Reader& reader, ConnectRequest& output) noexcept {
    return read_sequences(reader, output.channelId, output.sequence)
           && bits::read_raw(reader, output.address);
}

bool write_response(bits::Writer& writer, const ConnectResponse& body) noexcept {
    // the echoed pair must lead; the requester closes the connection when it doesn't match
    return write_sequences(writer, body.remoteChannelId, body.remoteSequence)
           && write_sequences(writer, body.channelId, body.sequence)
           && bits::write_raw(writer, body.address);
}

bool read_establish(bits::Reader& reader, ConnectEstablish& output) noexcept {
    return read_sequences(reader, output.remoteChannelId, output.channelId);
}

bool write_establish(bits::Writer& writer, const ConnectEstablish& body) noexcept {
    return write_sequences(writer, body.remoteChannelId, body.channelId);
}

bool write_refuse(bits::Writer& writer, const ConnectEnd& body) noexcept {
    return write_sequences(writer, body.remoteChannelId, body.remoteSequence)
           && writer.write(body.reason, kRefuseReasonWidth);
}

bool write_closed(bits::Writer& writer, const ConnectEnd& body) noexcept {
    return write_sequences(writer, body.remoteChannelId, body.remoteSequence)
           && writer.write(body.reason, kCloseReasonWidth);
}

bool read_closed(bits::Reader& reader, ConnectEnd& output) noexcept {
    std::uint64_t reason = 0;
    if (!read_sequences(reader, output.remoteChannelId, output.remoteSequence)
        || !reader.read(kCloseReasonWidth, reason)) {
        return false;
    }
    output.reason = static_cast<std::uint8_t>(reason);
    return true;
}

} // namespace dawn::middleware::gameplay::peer
