#include "../../encoding/bit_reader.h"
#include "opcode504.h"

namespace dawn::middleware::web_service::messages::opcode504 {
namespace {

constexpr std::uint8_t kCharacterSoidWidth = 64;
constexpr std::size_t kMinimumPayloadSize = 8;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode || message.payload.size() < kMinimumPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    return reader.read(kCharacterSoidWidth, request.characterSoid);
}

} // namespace dawn::middleware::web_service::messages::opcode504
