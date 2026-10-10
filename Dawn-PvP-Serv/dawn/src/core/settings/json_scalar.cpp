#include "parser.h"

namespace dawn::core::settings::parser {
namespace {

constexpr unsigned char kFirstPrintableCodePoint = 0x20;
constexpr std::string_view kSimpleEscapeCodes = "\"\\/bfnrt";
constexpr char kUnicodeEscapeCode = 'u';
constexpr std::size_t kUnicodeEscapeDigitCount = 4;

[[nodiscard]] constexpr bool is_hex_digit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F')
           || (value >= 'a' && value <= 'f');
}

} // namespace

bool Parser::string(std::string_view& output) noexcept {
    whitespace();
    if (position_ >= input_.size() || input_[position_] != '"') {
        return false;
    }
    const std::size_t start = ++position_;
    while (position_ < input_.size()) {
        const char value = input_[position_++];
        if (value == '"') {
            output = input_.substr(start, position_ - start - 1);
            return true;
        }
        if (static_cast<unsigned char>(value) < kFirstPrintableCodePoint) {
            return false;
        }
        if (value == '\\') {
            if (position_ >= input_.size()) {
                return false;
            }
            const char escape = input_[position_++];
            if (escape != kUnicodeEscapeCode) {
                if (kSimpleEscapeCodes.find(escape) == std::string_view::npos) {
                    return false;
                }
                continue;
            }

            if (input_.size() - position_ < kUnicodeEscapeDigitCount) {
                return false;
            }
            for (std::size_t index = 0; index < kUnicodeEscapeDigitCount; ++index) {
                if (!is_hex_digit(input_[position_ + index])) {
                    return false;
                }
            }
            position_ += kUnicodeEscapeDigitCount;
        }
    }
    return false;
}

bool Parser::boolean(bool& output) noexcept {
    if (literal("true")) {
        output = true;
        return true;
    }
    if (literal("false")) {
        output = false;
        return true;
    }
    return false;
}

bool Parser::literal(std::string_view value) noexcept {
    whitespace();
    if (input_.substr(position_, value.size()) != value) {
        return false;
    }
    position_ += value.size();
    return true;
}

bool Parser::level_value(std::string_view name, log::Level& output) noexcept {
    if (name == "error") {
        output = log::Level::error;
    } else if (name == "warn") {
        output = log::Level::warn;
    } else if (name == "info") {
        output = log::Level::info;
    } else if (name == "debug") {
        output = log::Level::debug;
    } else if (name == "off") {
        output = log::Level::off;
    } else {
        return false;
    }
    return true;
}

std::size_t Parser::channel_index(std::string_view name) noexcept {
    if (name == "core") {
        return static_cast<std::size_t>(log::Channel::core);
    }
    if (name == "client") {
        return static_cast<std::size_t>(log::Channel::client);
    }
    if (name == "state") {
        return static_cast<std::size_t>(log::Channel::state);
    }
    if (name == "server") {
        return static_cast<std::size_t>(log::Channel::server);
    }
    if (name == "middleware") {
        return static_cast<std::size_t>(log::Channel::middleware);
    }
    return static_cast<std::size_t>(log::Channel::count);
}

} // namespace dawn::core::settings::parser
