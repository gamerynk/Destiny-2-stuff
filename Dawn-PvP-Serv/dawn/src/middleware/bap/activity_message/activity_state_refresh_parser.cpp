#include "activity_state_refresh_parser.h"

#include <bit>

#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_message::state_refresh {
namespace {

constexpr std::uint32_t kBubbleIndexBias = 0x80000000U;

} // namespace

bool parse_state_refresh(std::span<const std::byte> input, StateRefresh& refresh) noexcept {
    refresh = {};
    if (input.size() < kEncodedSize) {
        return false;
    }

    StateRefresh parsed{};
    parsed.membershipRevision = encoding::read_u32_be(input.first<encoding::kU32Size>());
    const std::uint32_t bubbleWire =
        encoding::read_u32_be(input.subspan<encoding::kU32Size, encoding::kU32Size>());
    parsed.bubbleIndex = std::bit_cast<std::int32_t>(bubbleWire - kBubbleIndexBias);
    refresh = parsed;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::state_refresh
