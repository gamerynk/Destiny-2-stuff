#include "activity_manager_response.h"

#include <algorithm>
#include <array>

#include "../../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_host_manager::response {
namespace {

constexpr std::size_t kActivityDataSize = 128;

struct ResponseBody final {
    std::byte discriminator;
    std::array<std::byte, encoding::kU64Size> sessionId;
    std::array<std::byte, kActivityDataSize> activityData;
};

// service 7 rejects discriminator zero or anything other than 2
constexpr std::byte kResponseDiscriminator{2};
constexpr std::uint64_t kInvalidSessionId = 0;
constexpr std::size_t kNoBytesWritten = 0;
constexpr std::size_t kResponseBodySize = 137;

static_assert(sizeof(ResponseBody) == kResponseBodySize);

} // namespace

bool encode_response(std::uint64_t sessionId,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = kNoBytesWritten;
    if (sessionId == kInvalidSessionId || output.size() < kResponseBodySize) {
        return false;
    }

    ResponseBody body{};
    body.discriminator = kResponseDiscriminator;
    encoding::write_u64_be(body.sessionId, sessionId);

    const auto bytes = std::as_bytes(std::span{&body, 1});
    std::copy(bytes.begin(), bytes.end(), output.begin());
    written = bytes.size();
    return true;
}

} // namespace dawn::middleware::bap::activity_host_manager::response
