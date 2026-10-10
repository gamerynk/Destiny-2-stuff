#include "activity_entity_slot_request_parser.h"

#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_message::entity_slot_request {
namespace {

constexpr std::int64_t kSignedValueBias = 0x80000000LL;

} // namespace

bool parse_entity_slot_request(std::span<const std::byte> input, std::int32_t& value) noexcept {
    value = {};
    if (input.size() < kEncodedSize) {
        return false;
    }

    const std::uint32_t raw = encoding::read_u32_be(input.first<kEncodedSize>());
    const std::int64_t decoded = static_cast<std::int64_t>(raw) - kSignedValueBias;
    value = static_cast<std::int32_t>(decoded);
    return true;
}

} // namespace dawn::middleware::bap::activity_message::entity_slot_request
