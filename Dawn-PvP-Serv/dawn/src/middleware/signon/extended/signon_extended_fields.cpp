#include "signon_extended_fields.h"

#include <array>
#include <cstdio>
#include <span>
#include <string_view>

namespace dawn::middleware::signon::extended {
namespace {

constexpr unsigned kExtendedField = 12;
constexpr unsigned kFirstUrlField = 14;
constexpr unsigned kExtendedFlagField = 1;
constexpr unsigned kExtendedFirstBlobField = 3;
constexpr unsigned kExtendedSecondBlobField = 4;
constexpr std::uint64_t kExtendedFlag = 1;
constexpr std::size_t kExtendedBlobSize = 16;
// any non-secret filler works; neither blob has a proven client reader
constexpr std::byte kFirstBlobFill{0x11};
constexpr std::byte kSecondBlobFill{0x22};
constexpr std::size_t kExtendedBufferSize = 64;
constexpr std::array<char, 3> kUrlSuffixes{'a', 'b', 'c'};
constexpr std::size_t kUrlCapacity = 48;
constexpr unsigned kAddressByteBits = 8;
constexpr std::uint32_t kAddressByteMask = 0xFF;

[[nodiscard]] bool format_url(std::uint32_t relayAddress,
                              char suffix,
                              std::span<char> output,
                              std::size_t& size) noexcept {
    size = 0;
    const int written = std::snprintf(output.data(),
                                      output.size(),
                                      "http://%u.%u.%u.%u/cfg_%c/",
                                      relayAddress & kAddressByteMask,
                                      (relayAddress >> kAddressByteBits) & kAddressByteMask,
                                      (relayAddress >> (2 * kAddressByteBits)) & kAddressByteMask,
                                      (relayAddress >> (3 * kAddressByteBits)) & kAddressByteMask,
                                      suffix);
    if (written <= 0 || static_cast<std::size_t>(written) >= output.size()) {
        return false;
    }
    size = static_cast<std::size_t>(written);
    return true;
}

[[nodiscard]] bool encode_extended(std::span<std::byte> output, std::size_t& size) noexcept {
    size = 0;
    std::array<std::byte, kExtendedBlobSize> first{};
    std::array<std::byte, kExtendedBlobSize> second{};
    first.fill(kFirstBlobFill);
    second.fill(kSecondBlobFill);
    Writer writer(output);
    if (!writer.varint(kExtendedFlagField, kExtendedFlag)
        || !writer.bytes(kExtendedFirstBlobField, first)
        || !writer.bytes(kExtendedSecondBlobField, second)) {
        return false;
    }
    size = writer.size();
    return true;
}

} // namespace

bool append(Writer& success, std::uint32_t relayAddress) noexcept {
    std::array<std::byte, kExtendedBufferSize> extended{};
    std::size_t extendedSize = 0;
    if (!encode_extended(extended, extendedSize)
        || !success.bytes(kExtendedField, std::span(extended).first(extendedSize))) {
        return false;
    }
    for (std::size_t index = 0; index < kUrlSuffixes.size(); ++index) {
        std::array<char, kUrlCapacity> url{};
        std::size_t length = 0;
        if (!format_url(relayAddress, kUrlSuffixes[index], url, length)
            || !success.bytes(kFirstUrlField + static_cast<unsigned>(index),
                              text_bytes(std::string_view(url.data(), length)))) {
            return false;
        }
    }
    return true;
}

} // namespace dawn::middleware::signon::extended
