#include "family_subscription.h"

#include "../encoding/byte_order.h"

namespace dawn::middleware::bap::family_subscription {
namespace {

constexpr std::size_t kFamilyTypeOffset = 0;
// 8-byte big-endian root id follows the family selector
constexpr std::size_t kFamilyRootOffset = kFamilyTypeOffset + sizeof(std::uint8_t);
constexpr std::size_t kBodySize = kFamilyRootOffset + encoding::kU64Size;

} // namespace

bool parse(std::span<const std::byte> input, queuez::Subscription& subscription) noexcept {
    subscription = {};
    // covers only the two reads below, not a rule about the client's body
    if (input.size() < kBodySize) {
        return false;
    }
    subscription.familyType = std::to_integer<std::uint8_t>(input[kFamilyTypeOffset]);
    subscription.familyRootSoid =
        encoding::read_u64_be(std::span<const std::byte, encoding::kU64Size>(
            input.data() + kFamilyRootOffset, encoding::kU64Size));
    return true;
}

} // namespace dawn::middleware::bap::family_subscription
