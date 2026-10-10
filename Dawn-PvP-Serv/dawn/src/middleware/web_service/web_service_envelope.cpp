#include "web_service_envelope.h"

#include "../encoding/bit_writer.h"
#include "../encoding/byte_order.h"
#include "status_fields.h"

namespace dawn::middleware::web_service {

bool parse_request(std::span<const std::byte> input, Message& message) noexcept {
    message = {};
    if (input.size() < kEnvelopeHeaderSize) {
        return false;
    }
    message.opcode = encoding::read_u16_be(input.first<encoding::kU16Size>());
    message.transactionId =
        encoding::read_u32_be(input.subspan<encoding::kU16Size, encoding::kU32Size>());
    message.payload = input.subspan(kEnvelopeHeaderSize);
    return true;
}

bool encode_response(const Message& request,
                     ResponseShape shape,
                     const StatusResponse& status,
                     std::span<std::byte> output,
                     std::size_t& written,
                     std::span<const std::byte> queuezUpdate) noexcept {
    written = 0;
    if (queuezUpdate.size() > 65535) return false;
    if (output.size() < kEnvelopeHeaderSize) {
        return false;
    }
    encoding::write_u16_be(output.first<encoding::kU16Size>(), request.opcode);
    encoding::write_u32_be(output.subspan<encoding::kU16Size, encoding::kU32Size>(),
                           request.transactionId);

    encoding::bits::Writer writer(output.subspan(kEnvelopeHeaderSize));
    bool encoded = dawn::middleware::web_service::status::write_fields(writer, shape, status);
    if (queuezUpdate.empty()) {
        encoded = encoded && writer.write(0, kAbsentTrailerWidth);
    } else {
        // native svc11 decoder 51C670: presence, unsigned16 byte length, raw bytes, second absent field
        encoded = encoded && writer.write(1, 1) && writer.write(queuezUpdate.size(), 16);
        for (const auto byte : queuezUpdate) encoded = encoded && writer.write(std::to_integer<unsigned>(byte), 8);
        encoded = encoded && writer.write(0, 1);
    }

    std::size_t payloadSize = 0;
    if (!encoded || !writer.finish(payloadSize)) {
        return false;
    }
    written = kEnvelopeHeaderSize + payloadSize;
    return true;
}

} // namespace dawn::middleware::web_service
