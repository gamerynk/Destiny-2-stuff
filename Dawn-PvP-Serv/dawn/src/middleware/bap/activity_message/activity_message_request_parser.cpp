#include "activity_message_request_parser.h"

#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_message {
namespace {

struct RequestLayout final {
    static constexpr std::size_t accountHandle = 0;
    static constexpr std::size_t discriminator = accountHandle + encoding::kU64Size;
    static constexpr std::size_t messageType = discriminator + sizeof(std::byte);
    static constexpr std::size_t payloadLength = messageType + encoding::kU32Size;
    static constexpr std::size_t peerHeardMask = payloadLength + encoding::kU32Size;
    static constexpr std::size_t payload = peerHeardMask + encoding::kU32Size;
};

} // namespace

bool parse_request(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    if (input.size() < RequestLayout::payload
        || input[RequestLayout::discriminator] != kTransportDiscriminator) {
        return false;
    }

    const std::uint32_t declaredLength =
        encoding::read_u32_be(input.subspan<RequestLayout::payloadLength, encoding::kU32Size>());
    const std::size_t availableLength = input.size() - RequestLayout::payload;
    if (declaredLength > kMaximumPayloadSize
        || static_cast<std::size_t>(declaredLength) != availableLength) {
        return false;
    }

    Request parsed{};
    parsed.accountHandle =
        encoding::read_u64_be(input.subspan<RequestLayout::accountHandle, encoding::kU64Size>());
    parsed.messageType =
        encoding::read_u32_be(input.subspan<RequestLayout::messageType, encoding::kU32Size>());
    parsed.peerHeardMask =
        encoding::read_u32_be(input.subspan<RequestLayout::peerHeardMask, encoding::kU32Size>());
    parsed.payload = input.subspan(RequestLayout::payload, availableLength);
    request = parsed;
    return true;
}

} // namespace dawn::middleware::bap::activity_message
