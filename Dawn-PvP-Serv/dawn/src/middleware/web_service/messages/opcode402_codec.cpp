#include <cstddef>
#include <limits>

#include "../../encoding/bit_reader.h"
#include "opcode402.h"

namespace dawn::middleware::web_service::messages::opcode402 {
namespace {

constexpr std::size_t kPayloadSize = 16;
constexpr std::uint8_t kInstanceWidth = 64;
constexpr std::uint8_t kDefinitionIndexWidth = 15;
constexpr std::uint8_t kQuantityWidth = 32;
constexpr std::uint32_t kSingleQuantityWire = 0x80000001U;
constexpr std::uint8_t kRequiredFlagWidth = 1;
// bucket ordinal the request came from, counting 0 upward; the instance identity already names the
// item, so it is read and not acted on
constexpr std::uint8_t kBucketOrdinalWidth = 7;
constexpr std::uint8_t kNestedPaddingWidth = 1;
constexpr std::uint8_t kFinalPaddingWidth = 6;

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
    std::uint64_t encodedDefinitionIndex = 0;
    std::uint64_t encodedQuantity = 0;
    std::uint64_t requiredFlag = 0;
    std::uint64_t bucketOrdinal = 0;
    std::uint64_t nestedPadding = 0;
    std::uint64_t finalPadding = 0;
    const bool read =
        message.payload.size() == kPayloadSize && reader.read(1, instancePresent)
        && reader.read(kInstanceWidth, instanceSoid) && reader.read(1, definitionPresent)
        && reader.read(kDefinitionIndexWidth, encodedDefinitionIndex)
        && reader.read(kQuantityWidth, encodedQuantity)
        && reader.read(kRequiredFlagWidth, requiredFlag)
        && reader.read(kBucketOrdinalWidth, bucketOrdinal)
        && reader.read(kNestedPaddingWidth, nestedPadding)
        && reader.read(kFinalPaddingWidth, finalPadding) && reader.remaining_bits() == 0;

    request.instanceSoid = instanceSoid;
    if (encodedDefinitionIndex <= (std::numeric_limits<std::uint16_t>::max)()) {
        request.definitionIndex = static_cast<std::uint16_t>(encodedDefinitionIndex);
    }

    return read && instancePresent != 0 && definitionPresent != 0 && instanceSoid != 0
           && nestedPadding == 0 && finalPadding == 0
           && static_cast<std::uint32_t>(encodedQuantity) == kSingleQuantityWire
           && requiredFlag == 1;
}

} // namespace dawn::middleware::web_service::messages::opcode402
