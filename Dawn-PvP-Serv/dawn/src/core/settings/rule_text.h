#pragma once

#include <cstddef>
#include <cstdint>

namespace dawn::core::rule_text {

// a file too big for the buffer is refused whole, which silently turns off whatever it
// configures, so keep this far larger than any authored file
inline constexpr std::size_t kRuleTextCapacity = 65536;

inline constexpr char kCommentMark = '#';

[[nodiscard]] constexpr bool is_hex_digit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f')
           || (value >= 'A' && value <= 'F');
}

[[nodiscard]] constexpr std::uint32_t hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint32_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint32_t>(value - 'a') + 10U;
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint32_t>(value - 'A') + 10U;
    }
    return 0;
}

class Cursor final {
public:
    explicit constexpr Cursor(const char* text) noexcept : at_{text} {}

    // a leading minus counts only before a digit, so a negative field never reads as positive
    [[nodiscard]] constexpr bool at_field() const noexcept {
        return is_hex_digit(*at_) || (*at_ == '-' && at_[1] >= '0' && at_[1] <= '9');
    }

    constexpr bool seek_field() noexcept {
        while (*at_ != '\0') {
            if (*at_ == kCommentMark) {
                while (*at_ != '\0' && *at_ != '\n') {
                    ++at_;
                }
                continue;
            }
            if (at_field()) {
                return true;
            }
            ++at_;
        }
        return false;
    }

    constexpr std::uint32_t read_hex() noexcept {
        std::uint32_t value = 0;
        while (is_hex_digit(*at_)) {
            value = (value * 16U) + hex_value(*at_);
            ++at_;
        }
        skip_spaces();
        return value;
    }

    // saturates instead of wrapping: overflow would give a small or negative value that looks
    // like a plausible rule
    constexpr std::int32_t read_decimal() noexcept {
        const bool negative = *at_ == '-';
        if (negative) {
            ++at_;
        }
        constexpr std::int64_t kCeiling = 0x7FFFFFFF;
        std::int64_t value = 0;
        while (*at_ >= '0' && *at_ <= '9') {
            if (value <= kCeiling) {
                value = (value * 10) + (*at_ - '0');
            }
            ++at_;
        }
        skip_spaces();
        if (value > kCeiling) {
            value = kCeiling;
        }
        return static_cast<std::int32_t>(negative ? -value : value);
    }

private:
    constexpr void skip_spaces() noexcept {
        while (*at_ == ' ' || *at_ == '\t') {
            ++at_;
        }
    }

    const char* at_;
};

} // namespace dawn::core::rule_text
