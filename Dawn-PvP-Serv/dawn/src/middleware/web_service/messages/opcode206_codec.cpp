#include "../../encoding/bit_reader.h"
#include "opcode206.h"

namespace dawn::middleware::web_service::messages::opcode206 {
namespace {

constexpr std::uint8_t kFamilyTypeWidth = 4;
constexpr std::uint64_t kFamilyTypeBias = 1;
constexpr std::uint8_t kFamilyRootWidth = 64;
// 68 meaningful bits need 9 bytes with the zero padding
constexpr std::size_t kMinimumPayloadSize = 9;

} // namespace

bool parse_request(const Message& message, queuez::Subscription& subscription) noexcept {
    subscription = {};
    if (message.opcode != kOpcode || message.payload.size() < kMinimumPayloadSize) {
        return false;
    }
    encoding::bits::Reader reader(message.payload);
    std::uint64_t storedFamily = 0;
    if (!reader.read(kFamilyTypeWidth, storedFamily)
        || !reader.read(kFamilyRootWidth, subscription.familyRootSoid)) {
        subscription = {};
        return false;
    }
    subscription.familyType = static_cast<std::uint32_t>(storedFamily - kFamilyTypeBias);
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode206
