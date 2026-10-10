#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <algorithm>

namespace dawn::middleware::content::bootstrap_configuration {
inline constexpr std::size_t kTokenSize = 16;
inline constexpr std::size_t kUrlCapacity = 128;
inline constexpr std::size_t kProofCapacity = 200;
inline constexpr std::size_t kHeaderSize = 24;
inline constexpr std::size_t kCapacity = kHeaderSize + kProofCapacity + kUrlCapacity - 1;
inline constexpr std::byte kVersion{2};
inline constexpr std::size_t kGuidSize = 36;
inline constexpr std::size_t kAcknowledgmentSize = 4 + kGuidSize;
struct View final {
    std::span<const std::byte> token;
    std::span<const std::byte> proof;
    std::string_view url;
    bool requestsNativeAcknowledgment{};
};
inline bool valid_guid(std::string_view guid) noexcept {
    if (guid.size() != kGuidSize) return false;
    for (std::size_t i = 0; i < guid.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (guid[i] != '-') return false;
        } else if (!((guid[i] >= '0' && guid[i] <= '9') || (guid[i] >= 'a' && guid[i] <= 'f'))) return false;
    }
    return true;
}
inline bool acknowledge(bool native, std::string_view guid, std::span<std::byte> output,
                        std::size_t& written) noexcept {
    written = 0;
    const std::size_t prefix = native ? 4U : 0U;
    if (!valid_guid(guid) || output.size() < prefix + kGuidSize) return false;
    if (native) {
        output[0] = std::byte{'D'}; output[1] = std::byte{'C'};
        output[2] = std::byte{'A'}; output[3] = std::byte{1};
    }
    std::copy(guid.begin(), guid.end(), reinterpret_cast<char*>(output.data() + prefix));
    written = prefix + kGuidSize;
    return true;
}
// GUID-only responses stay valid but never authorize removing the getter hooks
inline bool parse_acknowledgment(std::span<const std::byte> input, std::string_view& guid,
                                bool& native) noexcept {
    guid = {}; native = false;
    if (input.size() != kGuidSize && input.size() != kAcknowledgmentSize) return false;
    const bool candidateNative = input.size() == kAcknowledgmentSize;
    if (candidateNative && (input[0] != std::byte{'D'} || input[1] != std::byte{'C'}
        || input[2] != std::byte{'A'} || input[3] != std::byte{1})) return false;
    const std::string_view candidate{reinterpret_cast<const char*>(input.data() + (candidateNative ? 4U : 0U)), kGuidSize};
    if (!valid_guid(candidate)) return false;
    guid = candidate; native = candidateNative;
    return true;
}
inline bool valid_url(std::string_view url) noexcept {
    if (url.empty() || url.size() >= kUrlCapacity) return false;
    const auto ascii_equal = [](char a, char b) noexcept {
        return (a >= 'A' && a <= 'Z' ? a + ('a' - 'A') : a) == b;
    };
    const auto scheme = [&url, &ascii_equal](std::string_view expected) noexcept {
        if (url.size() < expected.size()) return false;
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (!ascii_equal(url[i], expected[i])) return false;
        return true;
    };
    const auto start = scheme("http://") ? 7U : scheme("https://") ? 8U : 0U;
    if (start == 0 || start >= url.size()) return false;
    const auto end = url.find_first_of("/?#", start);
    const auto authority = url.substr(start, end == url.npos ? url.size() - start : end - start);
    if (authority.empty() || authority.find('@') != authority.npos) return false;
    for (const unsigned char value : url)
        if (value <= 0x20 || value > 0x7E || value == '\\') return false;
    return true;
}
inline bool encode(std::span<const std::byte> token, std::span<const std::byte> proof,
                   std::string_view url, std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written = 0;
    if (token.size() != kTokenSize || (proof.size() != 128 && proof.size() != kProofCapacity)
        || !valid_url(url) || output.size() < kHeaderSize + proof.size() + url.size()) return false;
    output[0] = std::byte{'D'}; output[1] = std::byte{'C'};
    output[2] = std::byte{'F'}; output[3] = kVersion;
    output[4] = static_cast<std::byte>(proof.size()); output[5] = std::byte{};
    output[6] = static_cast<std::byte>(url.size()); output[7] = std::byte{};
    std::copy(token.begin(), token.end(), output.begin() + 8);
    std::copy(proof.begin(), proof.end(), output.begin() + kHeaderSize);
    std::copy(url.begin(), url.end(), reinterpret_cast<char*>(output.data() + kHeaderSize + proof.size()));
    written = kHeaderSize + proof.size() + url.size();
    return true;
}
inline bool parse(std::span<const std::byte> input, View& output) noexcept {
    output = {};
    if (input.size() < kHeaderSize || input[0] != std::byte{'D'} || input[1] != std::byte{'C'}
        || input[2] != std::byte{'F'} || (input[3] != kVersion && input[3] != std::byte{1})
        || input[5] != std::byte{} || input[7] != std::byte{}) return false;
    const auto proofSize = std::to_integer<std::size_t>(input[4]);
    const auto urlSize = std::to_integer<std::size_t>(input[6]);
    if ((proofSize != 128 && proofSize != kProofCapacity) || urlSize >= kUrlCapacity
        || input.size() != kHeaderSize + proofSize + urlSize) return false;
    View candidate{input.subspan(8, kTokenSize), input.subspan(kHeaderSize, proofSize),
        {reinterpret_cast<const char*>(input.data() + kHeaderSize + proofSize), urlSize}, input[3] == kVersion};
    if (!valid_url(candidate.url)) return false;
    output = candidate;
    return true;
}
}
