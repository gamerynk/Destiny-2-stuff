#include "client_config_response.h"

#include <cstdint>

namespace dawn::middleware::bap::client_config {
namespace {

constexpr std::uint8_t kFirstRequiredFieldNumber = 1;
constexpr std::uint8_t kSecondRequiredFieldNumber = 2;
constexpr std::uint8_t kWireTypeBitCount = 3;
constexpr std::uint8_t kVarintWireType = 0;
constexpr std::byte kRequiredFieldValue{0x00};
constexpr std::size_t kFirstFieldTagOffset = 0;
constexpr std::size_t kFirstFieldValueOffset = kFirstFieldTagOffset + 1;
constexpr std::size_t kSecondFieldTagOffset = kFirstFieldValueOffset + 1;
constexpr std::size_t kSecondFieldValueOffset = kSecondFieldTagOffset + 1;
constexpr std::size_t kResponseSize = kSecondFieldValueOffset + 1;

constexpr std::byte field_key(std::uint8_t fieldNumber) noexcept {
    return static_cast<std::byte>((fieldNumber << kWireTypeBitCount) | kVarintWireType);
}

} // namespace

bool encode_minimal_response(std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kResponseSize) {
        return false;
    }
    output[kFirstFieldTagOffset] = field_key(kFirstRequiredFieldNumber);
    output[kFirstFieldValueOffset] = kRequiredFieldValue;
    output[kSecondFieldTagOffset] = field_key(kSecondRequiredFieldNumber);
    output[kSecondFieldValueOffset] = kRequiredFieldValue;
    written = kResponseSize;
    return true;
}

} // namespace dawn::middleware::bap::client_config
