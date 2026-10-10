#include <cstddef>
#include <span>

#include "../../encoding/byte_order.h"
#include "opcode403.h"

namespace dawn::middleware::web_service::messages::opcode403 {
namespace {

constexpr std::size_t kPayloadSize = encoding::kU64Size + 1U;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if ((message.opcode != kOpcode && message.opcode != kUnequipOpcode)
        || message.payload.size() != kPayloadSize
        || message.payload[encoding::kU64Size] != std::byte{}) {
        return false;
    }
    request.instanceSoid = encoding::read_u64_be(
        std::span<const std::byte, encoding::kU64Size>{message.payload.data(), encoding::kU64Size});
    if (request.instanceSoid == 0) {
        request = {};
        return false;
    }
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode403
