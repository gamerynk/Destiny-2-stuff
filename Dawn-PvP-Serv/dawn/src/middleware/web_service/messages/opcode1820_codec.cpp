#include <cstddef>

#include "../../encoding/bit_reader.h"
#include "opcode1820.h"

namespace dawn::middleware::web_service::messages::opcode1820 {
namespace {

constexpr std::size_t kPayloadSize = 3;
constexpr std::uint8_t kPresenceWidth = 1;
constexpr std::uint8_t kCollectibleIndexWidth = 15;
// the descriptor pads its two payload bytes out to three
constexpr std::uint8_t kPaddingWidth = 8;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() != kPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t present = 0;
    std::uint64_t encodedCollectibleIndex = 0;
    std::uint64_t padding = 0;
    if (!reader.read(kPresenceWidth, present)
        || !reader.read(kCollectibleIndexWidth, encodedCollectibleIndex)
        || !reader.read(kPaddingWidth, padding) || reader.remaining_bits() != 0 || present == 0
        || padding != 0) {
        return false;
    }
    request.collectibleIndex = static_cast<std::uint16_t>(encodedCollectibleIndex);
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode1820
