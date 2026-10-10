#include "parser.h"

namespace dawn::core::settings::parser {
namespace {

constexpr unsigned kMaximumJsonDepth = 16;

} // namespace

bool Parser::skip_value(unsigned depth) noexcept {
    if (depth >= kMaximumJsonDepth) {
        return false;
    }
    whitespace();
    if (position_ >= input_.size()) {
        return false;
    }

    if (input_[position_] == '"') {
        std::string_view ignored;
        return string(ignored);
    }
    if (input_[position_] == '{') {
        ++position_;
        if (consume('}')) {
            return true;
        }
        for (;;) {
            std::string_view ignored;
            if (!string(ignored) || !consume(':') || !skip_value(depth + 1)) {
                return false;
            }
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }
    if (input_[position_] == '[') {
        ++position_;
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (!skip_value(depth + 1)) {
                return false;
            }
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }
    if (literal("true") || literal("false") || literal("null")) {
        return true;
    }
    return number();
}

bool Parser::consume(char value) noexcept {
    whitespace();
    if (position_ >= input_.size() || input_[position_] != value) {
        return false;
    }
    ++position_;
    return true;
}

bool Parser::at_end() noexcept {
    whitespace();
    return position_ == input_.size();
}

void Parser::whitespace() noexcept {
    while (position_ < input_.size()) {
        const char value = input_[position_];
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
            return;
        }
        ++position_;
    }
}

} // namespace dawn::core::settings::parser
