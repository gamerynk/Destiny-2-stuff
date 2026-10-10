#include "family_unsubscription.h"

#include "../encoding/byte_order.h"

namespace dawn::middleware::bap::family_unsubscription {
namespace {

constexpr std::size_t kFamilyTypeOffset = 0;
constexpr std::size_t kFamilyRootOffset = kFamilyTypeOffset + sizeof(std::uint8_t);
constexpr std::size_t kBodySize = kFamilyRootOffset + encoding::kU64Size;

} // namespace

bool parse(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    if (input.size() < kBodySize) {
        return false;
    }
    request.familyType = std::to_integer<std::uint8_t>(input[kFamilyTypeOffset]);
    request.familyRootSoid = encoding::read_u64_be(std::span<const std::byte, encoding::kU64Size>(
        input.data() + kFamilyRootOffset, encoding::kU64Size));
    return true;
}

} // namespace dawn::middleware::bap::family_unsubscription
