#include "signon_config_blob.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include "../../content/bootstrap_configuration.h"

namespace dawn::middleware::signon::config {
namespace {

// blob layout the client's config applier parses; key 0 ends its entry loop
constexpr std::uint32_t kMagic = 0x00123456;
constexpr std::uint32_t kTokenKey = 0x007B620F;
constexpr std::uint32_t kTerminator = 0;
constexpr std::size_t kTokenKeyOffset = 4;
constexpr std::size_t kLengthOffset = 8;
constexpr std::size_t kTokenOffset = 9;
constexpr std::uint32_t kEventPrioritiesKey = 0x206;
constexpr std::uint32_t kUrgencyThresholdKey = 0x1A39E5;
// retail 5A7300 reset defaults with selected combat priorities raised; generic messages also
// prioritize noncombat script messages, reloads and throws use native dynamic upper bounds
constexpr std::array<float, 22> kEventPriorities{
    0.0F, .95F, .95F, .8F, .8F, 0.0F, 0.0F, .73F, .9F, 0.0F, 0.0F,
    0.0F, .7F, .75F, 0.0F, 0.0F, .95F, .7F, .7F, .7F, .7F, .9F,
};
// native first-unsent urgency compares quantized priority/1023 with >; the far initial
// priority quantizes to 51 (.04985), so keep it urgent
constexpr float kUrgencyThreshold = .04F;

void write_fixed32(std::span<std::byte> output, std::size_t offset, std::uint32_t value) noexcept {
    output[offset] = static_cast<std::byte>(value);
    output[offset + 1] = static_cast<std::byte>(value >> 8U);
    output[offset + 2] = static_cast<std::byte>(value >> 16U);
    output[offset + 3] = static_cast<std::byte>(value >> 24U);
}

} // namespace

bool build(std::span<const std::byte> token, std::span<std::byte> output) noexcept {
    if (token.size() != state::kBootstrapTokenSize || output.size() != kBlobSize) return false;
    write_fixed32(output, 0, kMagic);
    write_fixed32(output, kTokenKeyOffset, kTokenKey);
    output[kLengthOffset] = static_cast<std::byte>(token.size());
    std::copy(token.begin(), token.end(), output.begin() + kTokenOffset);
    auto offset = kTokenOffset + token.size();
    write_fixed32(output, offset, kEventPrioritiesKey);
    offset += 4;
    for (const float priority : kEventPriorities) {
        write_fixed32(output, offset, std::bit_cast<std::uint32_t>(priority));
        offset += 4;
    }
    write_fixed32(output, offset, kUrgencyThresholdKey);
    write_fixed32(output, offset + 4, std::bit_cast<std::uint32_t>(kUrgencyThreshold));
    write_fixed32(output, offset + 8, kTerminator);
    return true;
}

bool build(std::span<const std::byte> token, std::string_view url, std::string_view guid,
           std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (!content::bootstrap_configuration::valid_url(url) || guid.size() != 36) return false;
    for (std::size_t index = 0; index < guid.size(); ++index) {
        const char value = guid[index];
        const bool hyphen = index == 8 || index == 13 || index == 18 || index == 23;
        if (hyphen ? value != '-' : !((value >= '0' && value <= '9')
                                     || (value >= 'a' && value <= 'f'))) return false;
    }
    const auto size = kBlobSize + 2 * (4 + 1 + url.size()) + 4 + 1 + guid.size() + 5;
    if (output.size() < size || token.size() != state::kBootstrapTokenSize) return false;
    std::array<std::byte, kBlobSize> legacy{};
    if (!build(token, legacy)) return false;
    std::copy(legacy.begin(), legacy.end() - 4, output.begin());
    auto offset = kBlobSize - 4;
    const auto string = [&output, &offset](std::uint32_t key, std::string_view value) noexcept {
        write_fixed32(output, offset, key); offset += 4;
        output[offset++] = static_cast<std::byte>(value.size());
        std::copy(value.begin(), value.end(), reinterpret_cast<char*>(output.data() + offset));
        offset += value.size();
    };
    string(0x64, url);
    string(0x65, guid);
    string(0x66, url);
    write_fixed32(output, offset, 0x37E); offset += 4;
    output[offset++] = std::byte{};
    write_fixed32(output, offset, kTerminator); offset += 4;
    written = offset;
    return true;
}

} // namespace dawn::middleware::signon::config
