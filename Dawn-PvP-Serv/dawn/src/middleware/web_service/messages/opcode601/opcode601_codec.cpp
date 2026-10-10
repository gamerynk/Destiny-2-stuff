// 601 is a loot pickup the client can't do itself; the reply is 165 bits and a bare echo under-runs the decoder

#include "opcode601_codec.h"

#include <algorithm>
#include <array>

#include "../../../encoding/bit_writer.h"
#include "../../../encoding/bit_reader.h"
#include "../../../encoding/byte_order.h"
#include "../../status_fields.h"

namespace dawn::middleware::web_service::messages::opcode601 {
namespace {

constexpr std::uint8_t kTailIntegerWidth = 32;
constexpr std::uint8_t kTailLongWidth = 64;

} // namespace

bool parse_request(const Message& message, Request& output) noexcept {
    output = {};
    if (message.opcode != kOpcode || message.payload.size() != kRequestSize) return false;
    encoding::bits::Reader reader(message.payload);
    Request parsed{};
    std::uint64_t value{};
    if (!reader.read(5, value)) return false;
    parsed.rawKind = static_cast<std::uint8_t>(value);
    if (!reader.read(32, value)) return false;
    parsed.rawSourceTag = static_cast<std::uint32_t>(value);
    if (!reader.read(64, parsed.sourceHandle) || !reader.read(32, value)) return false;
    parsed.rawSequence = static_cast<std::uint32_t>(value);
    if (!reader.read(3, value) || value != 0 || reader.remaining_bits() != 0) return false;
    output = parsed;
    return true;
}

bool encode_response(const Message& message,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    Request request{};
    if (!parse_request(message, request) || output.size() < kResponseSize) {
        return false;
    }

    std::array<std::byte, kResponseSize> staged{};
    encoding::write_u16_be(std::span(staged).first<encoding::kU16Size>(), message.opcode);
    encoding::write_u32_be(std::span(staged).subspan<encoding::kU16Size, encoding::kU32Size>(),
                           message.transactionId);

    // status value is the family-4 version the client waits for; nothing is pushed, so the default is fine
    encoding::bits::Writer writer(std::span(staged).subspan(kEnvelopeHeaderSize));
    bool encoded = status::write_fields(writer, ResponseShape::statusPair, StatusResponse{});
    encoded = encoded && writer.write(request.rawSourceTag, kTailIntegerWidth)
              && writer.write(request.sourceHandle, kTailLongWidth)
              && writer.write(request.rawSequence, kTailIntegerWidth)
              && writer.write(0U, kAbsentTrailerWidth);

    std::size_t payloadSize = 0;
    if (!encoded || !writer.finish(payloadSize)) {
        return false;
    }
    const std::size_t total = kEnvelopeHeaderSize + payloadSize;
    if (output.size() < total) {
        return false;
    }
    std::copy_n(staged.begin(), total, output.begin());
    written = total;
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode601
