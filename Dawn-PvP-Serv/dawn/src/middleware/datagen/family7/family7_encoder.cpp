#include "family7_encoder.h"

#include <algorithm>
#include <cstring>

namespace dawn::middleware::datagen::family7 {
namespace {

constexpr std::size_t kBodyKeyOffset = 0x00;
constexpr std::size_t kDescriptorLengthOffset = 0x08;
constexpr std::size_t kDescriptorOffset = 0x0C;
constexpr std::size_t kPaddingOffset = 0x8C;
constexpr std::size_t kCompatibilityOffset = 0x90;
constexpr std::uint32_t kDescriptorLength = static_cast<std::uint32_t>(kJoinDescriptorSize);

template <typename Value>
void write(std::span<std::byte> destination,
           std::size_t offset,
           const Value& value) noexcept {
    std::memcpy(destination.data() + offset, &value, sizeof value);
}

} // namespace

static_assert(kDescriptorOffset + kJoinDescriptorSize == kPaddingOffset);
static_assert(kPaddingOffset + sizeof(std::uint32_t) == kCompatibilityOffset);
static_assert(kCompatibilityOffset + sizeof(std::uint64_t) == kBodySize);

bool encode_anchor(std::uint64_t accountSoid,
                   std::uint64_t bodySoid,
                   std::span<std::byte> destination) noexcept {
    if (accountSoid == 0 || bodySoid == 0 || destination.size() != kAnchorSize) {
        return false;
    }
    std::fill(destination.begin(), destination.end(), std::byte{});
    write(destination, 0, accountSoid);
    write(destination, sizeof accountSoid, bodySoid);
    return true;
}

bool encode_body(std::uint64_t bodySoid,
                 std::span<const std::byte> joinDescriptor,
                 std::uint64_t compatibilitySignature,
                 std::span<std::byte> destination) noexcept {
    if (bodySoid == 0 || joinDescriptor.size() != kJoinDescriptorSize
        || compatibilitySignature == 0 || destination.size() != kBodySize) {
        return false;
    }
    const bool descriptorPresent =
        std::any_of(joinDescriptor.begin(), joinDescriptor.begin() + sizeof(std::uint64_t),
                    [](std::byte value) { return value != std::byte{}; });
    if (!descriptorPresent) {
        return false;
    }

    std::fill(destination.begin(), destination.end(), std::byte{});
    write(destination, kBodyKeyOffset, bodySoid);
    write(destination, kDescriptorLengthOffset, kDescriptorLength);
    std::copy(joinDescriptor.begin(), joinDescriptor.end(), destination.begin() + kDescriptorOffset);
    // +0x8C..+0x8F is native alignment padding and stays zero
    write(destination, kCompatibilityOffset, compatibilitySignature);
    return true;
}

} // namespace dawn::middleware::datagen::family7
