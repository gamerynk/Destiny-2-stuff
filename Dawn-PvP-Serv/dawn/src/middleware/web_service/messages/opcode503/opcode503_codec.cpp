#include <algorithm>
#include <array>

#include "../../../encoding/byte_order.h"
#include "../../status_fields.h"
#include "../family5/family5_codec.h"
#include "../opcode503.h"

namespace dawn::middleware::web_service::messages::opcode503 {
namespace {

constexpr std::size_t kRequestTagSize = sizeof(std::uint8_t);
constexpr std::size_t kRequestPayloadSize = kRequestTagSize + encoding::kU64Size;
constexpr std::uint64_t kInvalidSoid = 0;
constexpr std::uint8_t kTopLevelIntegerWidth = 32;
constexpr std::uint32_t kUnusedIntegerValue = 0;
constexpr std::uint8_t kSoidWidth = 64;
constexpr std::size_t kMaximumResponseSize = 917;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode) {
        return false;
    }
    if (message.payload.size() < kRequestPayloadSize) {
        return true;
    }
    request.tag = std::to_integer<std::uint8_t>(message.payload.front());
    request.primarySoid =
        encoding::read_u64_be(message.payload.subspan<kRequestTagSize, encoding::kU64Size>());
    request.hasPrimarySoid = request.primarySoid != kInvalidSoid;
    return true;
}

bool encode_response(const Message& message,
                     const Request& request,
                     const state::InvestmentState& investment,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (message.opcode != kOpcode || !family5::valid(investment.family5)) {
        return false;
    }
    std::array<std::byte, kMaximumResponseSize> staged{};
    encoding::write_u16_be(std::span(staged).first<encoding::kU16Size>(), message.opcode);
    encoding::write_u32_be(std::span(staged).subspan<encoding::kU16Size, encoding::kU32Size>(),
                           message.transactionId);

    encoding::bits::Writer writer(std::span(staged).subspan(kEnvelopeHeaderSize));
    bool encoded = status::write_fields(writer, ResponseShape::statusPair, StatusResponse{});
    encoded = encoded && writer.write(request.primarySoid, kSoidWidth)
              && writer.write(kUnusedIntegerValue, kTopLevelIntegerWidth)
              && writer.write(kUnusedIntegerValue, kTopLevelIntegerWidth)
              && family5::write(writer, investment.family5)
              && writer.write(0U, kAbsentTrailerWidth);

    std::size_t payloadSize = 0;
    if (!encoded || !writer.finish(payloadSize)) {
        return false;
    }
    const std::size_t responseSize = kEnvelopeHeaderSize + payloadSize;
    if (responseSize > output.size()) {
        return false;
    }
    std::copy_n(staged.begin(), responseSize, output.begin());
    written = responseSize;
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode503
