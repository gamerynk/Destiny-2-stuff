#include <cstddef>
#include <limits>

#include "../../encoding/bit_reader.h"
#include "opcode406.h"

namespace dawn::middleware::web_service::messages::opcode406 {
namespace {

constexpr std::size_t kPayloadSize = 15;
constexpr std::uint8_t kDefinitionIndexWidth = 15;
constexpr std::uint8_t kValueWidth = 32;
constexpr std::uint8_t kPaddingWidth = 7;
constexpr std::uint64_t kValueBias = 0x80000000ULL;
constexpr std::uint64_t kSupportedStateBits = 0x3U;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t instancePresent = 0;
    std::uint64_t instanceSoid = 0;
    std::uint64_t definitionPresent = 0;
    std::uint64_t definitionIndex = 0;
    std::uint64_t encodedFlags = 0;
    std::uint64_t padding = 0;
    const bool read = message.payload.size() == kPayloadSize && reader.read(1, instancePresent)
                      && reader.read(64, instanceSoid) && reader.read(1, definitionPresent)
                      && reader.read(kDefinitionIndexWidth, definitionIndex)
                      && reader.read(kValueWidth, encodedFlags)
                      && reader.read(kPaddingWidth, padding) && reader.remaining_bits() == 0;

    request.instanceSoid = instanceSoid;
    if (definitionIndex <= (std::numeric_limits<std::uint16_t>::max)()) {
        request.definitionIndex = static_cast<std::uint16_t>(definitionIndex);
    }
    if (encodedFlags >= kValueBias) {
        request.flags = static_cast<std::uint32_t>(encodedFlags - kValueBias);
    }

    if (!read || instancePresent == 0 || instanceSoid == 0 || definitionPresent == 0
        || definitionIndex > (std::numeric_limits<std::uint16_t>::max)()
        || encodedFlags < kValueBias || padding != 0
        || encodedFlags - kValueBias > kSupportedStateBits) {
        return false;
    }
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode406
