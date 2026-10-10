#include <cstddef>
#include <cstdint>

#include "../../encoding/bit_reader.h"
#include "opcode405.h"

namespace dawn::middleware::web_service::messages::opcode405 {
namespace {

constexpr std::size_t kPayloadSize = 18;
constexpr std::uint8_t kAlignmentWidth = 8;
constexpr std::int64_t kInt16Bias = 0x8000;
constexpr std::int64_t kInt8Bias = 0x80;
constexpr std::int64_t kInt32Bias = 0x80000000LL;

} // namespace

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t unresolved16 = 0;
    std::uint64_t unresolved8 = 0;
    std::uint64_t itemInstanceSoid = 0;
    std::uint64_t definitionIndex = 0;
    std::uint64_t quantity = 0;
    std::uint64_t alignment = 0;
    const bool read = message.payload.size() == kPayloadSize && reader.read(16, unresolved16)
                      && reader.read(8, unresolved8) && reader.read(64, itemInstanceSoid)
                      && reader.read(16, definitionIndex) && reader.read(32, quantity)
                      && reader.read(kAlignmentWidth, alignment) && reader.remaining_bits() == 0;

    request.unresolved16 =
        static_cast<std::int16_t>(static_cast<std::int64_t>(unresolved16) - kInt16Bias);
    request.unresolved8 =
        static_cast<std::int8_t>(static_cast<std::int64_t>(unresolved8) - kInt8Bias);
    request.itemInstanceSoid = itemInstanceSoid;
    request.definitionIndex =
        static_cast<std::int16_t>(static_cast<std::int64_t>(definitionIndex) - kInt16Bias);
    request.quantity = static_cast<std::int32_t>(static_cast<std::int64_t>(quantity) - kInt32Bias);
    return read && alignment == 0 && itemInstanceSoid != 0;
}

} // namespace dawn::middleware::web_service::messages::opcode405
