#include "user_message_response.h"

#include <cstdint>

namespace dawn::middleware::bap::user_message {
namespace {

constexpr std::uint8_t kRequiredFieldNumber = 4;
constexpr std::uint8_t kWireTypeBitCount = 3;
constexpr std::uint8_t kVarintWireType = 0;
constexpr std::byte kRequiredFieldTag{
    static_cast<std::uint8_t>((kRequiredFieldNumber << kWireTypeBitCount) | kVarintWireType)};
constexpr std::byte kRequiredFieldValue{0x00};
constexpr std::size_t kFieldTagOffset = 0;
constexpr std::size_t kFieldValueOffset = kFieldTagOffset + 1;
constexpr std::size_t kResponseSize = kFieldValueOffset + 1;

} // namespace

bool encode_minimal_response(std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kResponseSize) {
        return false;
    }
    output[kFieldTagOffset] = kRequiredFieldTag;
    output[kFieldValueOffset] = kRequiredFieldValue;
    written = kResponseSize;
    return true;
}

} // namespace dawn::middleware::bap::user_message
