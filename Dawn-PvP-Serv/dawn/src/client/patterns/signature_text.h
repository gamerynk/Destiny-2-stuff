#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "registry.h"

namespace dawn::client::patterns {
namespace signature_detail {

inline constexpr char kWildcard = '?';
inline constexpr char kSeparator = ' ';
inline constexpr std::size_t kByteTokenLength = 2;
inline constexpr unsigned kFirstLetterValue = 10;
inline constexpr unsigned kNotHexDigit = 16;
inline constexpr unsigned kHighDigitShift = 4;

// declared and never defined, so calling it rejects malformed text at compile time
void malformed_signature_text();

[[nodiscard]] constexpr unsigned hex_value(char digit) noexcept {
    if (digit >= '0' && digit <= '9') {
        return static_cast<unsigned>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f') {
        return static_cast<unsigned>(digit - 'a') + kFirstLetterValue;
    }
    if (digit >= 'A' && digit <= 'F') {
        return static_cast<unsigned>(digit - 'A') + kFirstLetterValue;
    }
    return kNotHexDigit;
}

[[nodiscard]] constexpr bool valid_token(std::string_view token) noexcept {
    if (token.size() == 1) {
        return token[0] == kWildcard;
    }
    return token.size() == kByteTokenLength && hex_value(token[0]) != kNotHexDigit
           && hex_value(token[1]) != kNotHexDigit;
}

[[nodiscard]] constexpr std::size_t token_end(std::string_view text, std::size_t start) noexcept {
    std::size_t end = start;
    while (end < text.size() && text[end] != kSeparator) {
        ++end;
    }
    return end;
}

[[nodiscard]] constexpr std::byte token_byte(std::string_view token) noexcept {
    const unsigned value = (hex_value(token[0]) << kHighDigitShift) | hex_value(token[1]);
    return static_cast<std::byte>(value);
}

} // namespace signature_detail

[[nodiscard]] consteval std::size_t signature_length(std::string_view text) {
    std::size_t count = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] == signature_detail::kSeparator) {
            ++index;
            continue;
        }
        const std::size_t end = signature_detail::token_end(text, index);
        if (!signature_detail::valid_token(text.substr(index, end - index))) {
            signature_detail::malformed_signature_text();
        }
        ++count;
        index = end;
    }
    return count;
}

template <std::size_t Length>
[[nodiscard]] consteval std::array<PatternByte, Length> signature(std::string_view text) {
    std::array<PatternByte, Length> bytes{};
    std::size_t count = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] == signature_detail::kSeparator) {
            ++index;
            continue;
        }
        const std::size_t end = signature_detail::token_end(text, index);
        const std::string_view token = text.substr(index, end - index);
        if (count == Length || !signature_detail::valid_token(token)) {
            signature_detail::malformed_signature_text();
        }
        if (token[0] != signature_detail::kWildcard) {
            bytes[count] = PatternByte{signature_detail::token_byte(token), true};
        }
        ++count;
        index = end;
    }
    if (count != Length) {
        signature_detail::malformed_signature_text();
    }
    return bytes;
}

} // namespace dawn::client::patterns
