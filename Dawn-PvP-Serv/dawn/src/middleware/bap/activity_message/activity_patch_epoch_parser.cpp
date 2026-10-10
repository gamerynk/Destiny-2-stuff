#include "activity_patch_epoch_parser.h"

#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_message::patch_epoch {
namespace {

constexpr std::size_t kSecondFieldOffset = encoding::kU64Size;

} // namespace

bool parse_patch_epoch(std::span<const std::byte> input, PatchEpoch& epoch) noexcept {
    epoch = {};
    if (input.size() < kEncodedSize) {
        return false;
    }

    PatchEpoch parsed{};
    parsed.first = encoding::read_u64_be(input.first<encoding::kU64Size>());
    parsed.second = encoding::read_u64_be(input.subspan<kSecondFieldOffset, encoding::kU64Size>());
    epoch = parsed;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::patch_epoch
