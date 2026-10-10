#include "json_writer.h"

#include <array>
#include <cstdio>

namespace restoration::platform_api {
namespace {

constexpr std::string_view kHexDigits = "0123456789abcdef";

} // namespace

void JsonWriter::separate() {
    if (needComma_) {
        output_.push_back(',');
    }
    needComma_ = true;
}

void JsonWriter::begin_object() {
    separate();
    output_.push_back('{');
    needComma_ = false;
}

void JsonWriter::end_object() {
    output_.push_back('}');
    needComma_ = true;
}

void JsonWriter::begin_array() {
    separate();
    output_.push_back('[');
    needComma_ = false;
}

void JsonWriter::end_array() {
    output_.push_back(']');
    needComma_ = true;
}

void JsonWriter::key(std::string_view name) {
    separate();
    raw_string(name);
    output_.push_back(':');
    needComma_ = false;
}

void JsonWriter::string(std::string_view value) {
    separate();
    raw_string(value);
}

void JsonWriter::raw_string(std::string_view value) {
    output_.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output_ += "\\\""; break;
        case '\\': output_ += "\\\\"; break;
        case '\b': output_ += "\\b"; break;
        case '\f': output_ += "\\f"; break;
        case '\n': output_ += "\\n"; break;
        case '\r': output_ += "\\r"; break;
        case '\t': output_ += "\\t"; break;
        default:
            if (character < 0x20U) {
                // escape C0 controls instead of dropping them; dropping silently changes a display name
                output_ += "\\u00";
                output_.push_back(kHexDigits[(character >> 4U) & 0x0FU]);
                output_.push_back(kHexDigits[character & 0x0FU]);
            } else {
                // bytes >= 0x80 pass through so valid UTF-8 survives
                output_.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output_.push_back('"');
}

void JsonWriter::number(std::int64_t value) {
    separate();
    std::array<char, 24> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%lld",
                                      static_cast<long long>(value));
    if (written > 0) {
        output_.append(buffer.data(), static_cast<std::size_t>(written));
    }
}

void JsonWriter::number(std::uint64_t value) {
    separate();
    std::array<char, 24> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%llu",
                                      static_cast<unsigned long long>(value));
    if (written > 0) {
        output_.append(buffer.data(), static_cast<std::size_t>(written));
    }
}

void JsonWriter::number(double value) {
    separate();
    std::array<char, 32> buffer{};
    // %.9g round-trips floats without the long tail of %f
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.9g", value);
    if (written > 0) {
        output_.append(buffer.data(), static_cast<std::size_t>(written));
    }
}

void JsonWriter::boolean(bool value) {
    separate();
    output_ += value ? "true" : "false";
}

void JsonWriter::null() {
    separate();
    output_ += "null";
}

void JsonWriter::id(std::uint64_t value) {
    separate();
    std::array<char, 24> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%llu",
                                      static_cast<unsigned long long>(value));
    if (written <= 0) {
        output_ += "\"0\"";
        return;
    }
    output_.push_back('"');
    output_.append(buffer.data(), static_cast<std::size_t>(written));
    output_.push_back('"');
}

void JsonWriter::hash(std::uint32_t value) {
    number(static_cast<std::uint64_t>(value));
}

void JsonWriter::member(std::string_view name, std::string_view value) {
    key(name);
    string(value);
}

void JsonWriter::member(std::string_view name, std::int64_t value) {
    key(name);
    number(value);
}

void JsonWriter::member(std::string_view name, std::uint64_t value) {
    key(name);
    number(value);
}

void JsonWriter::member(std::string_view name, double value) {
    key(name);
    number(value);
}

void JsonWriter::member(std::string_view name, bool value) {
    key(name);
    boolean(value);
}

void JsonWriter::member_id(std::string_view name, std::uint64_t value) {
    key(name);
    id(value);
}

void JsonWriter::member_hash(std::string_view name, std::uint32_t value) {
    key(name);
    hash(value);
}

void JsonWriter::member_null(std::string_view name) {
    key(name);
    null();
}

} // namespace restoration::platform_api
