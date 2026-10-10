#include "activity_host_response.h"

#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_host {
namespace {

struct RequestLayout {
    static constexpr std::size_t activityHostId = 0;
    static constexpr std::size_t size = activityHostId + encoding::kU64Size;
};

struct ResponseLayout {
    static constexpr std::size_t activityHostId = RequestLayout::activityHostId;
    static constexpr std::size_t relayAddress = activityHostId + encoding::kU64Size;
    static constexpr std::size_t neutral = relayAddress + encoding::kU32Size;
    static constexpr std::size_t relayPort = neutral + encoding::kU16Size;
    static constexpr std::size_t size = relayPort + encoding::kU16Size;
};

constexpr std::uint32_t kInvalidRelayAddress = 0;
constexpr std::uint16_t kInvalidRelayPort = 0;
constexpr std::uint16_t kNeutralField = 0;

} // namespace

bool encode_response(std::span<const std::byte> requestBody,
                     std::uint32_t relayAddress,
                     std::uint16_t relayPort,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (requestBody.size() != RequestLayout::size || relayAddress == kInvalidRelayAddress
        || relayPort == kInvalidRelayPort || output.size() < ResponseLayout::size) {
        return false;
    }

    // echo every value, zero included; the client drops a mismatched reply and a missing
    // one jams its ring
    const std::uint64_t activityHostId = encoding::read_u64_be(
        requestBody.subspan<RequestLayout::activityHostId, encoding::kU64Size>());
    encoding::write_u64_be(output.subspan<ResponseLayout::activityHostId, encoding::kU64Size>(),
                           activityHostId);
    // SignOn carries the raw IPv4 scalar (first octet in the low byte); svc 17 sends it
    // as bytes, not BE
    for (std::size_t index = 0; index < encoding::kU32Size; ++index)
        output[ResponseLayout::relayAddress + index] =
            static_cast<std::byte>(relayAddress >> (index * encoding::kBitsPerByte));
    encoding::write_u16_be(output.subspan<ResponseLayout::neutral, encoding::kU16Size>(),
                           kNeutralField);
    encoding::write_u16_be(output.subspan<ResponseLayout::relayPort, encoding::kU16Size>(),
                           relayPort);
    written = ResponseLayout::size;
    return true;
}

} // namespace dawn::middleware::bap::activity_host
