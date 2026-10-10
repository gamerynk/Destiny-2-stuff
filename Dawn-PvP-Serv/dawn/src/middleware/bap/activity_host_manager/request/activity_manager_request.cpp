#include "activity_manager_request.h"

#include "../../../encoding/byte_order.h"

namespace dawn::middleware::bap::activity_host_manager::request {
namespace {

struct RequestLayout final {
    static constexpr std::size_t discriminator = 0;
    static constexpr std::size_t declaredLength = discriminator + sizeof(std::byte);
    static constexpr std::size_t protobuf = declaredLength + encoding::kU32Size;
    static constexpr std::size_t protobufCapacity = 7'714;
    static constexpr std::size_t size = protobuf + protobufCapacity;
};

constexpr std::byte kRequestDiscriminator{3};

} // namespace

bool parse_request(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    if (input.size() != RequestLayout::size
        || input[RequestLayout::discriminator] != kRequestDiscriminator) {
        return false;
    }

    const std::uint32_t declaredLength =
        encoding::read_u32_le(input.subspan<RequestLayout::declaredLength, encoding::kU32Size>());
    if (declaredLength > RequestLayout::protobufCapacity) {
        return false;
    }

    // only the declared region is borrowed; the padding after it is ignored on purpose
    request.protobuf =
        input.subspan(RequestLayout::protobuf, static_cast<std::size_t>(declaredLength));
    return true;
}

} // namespace dawn::middleware::bap::activity_host_manager::request
