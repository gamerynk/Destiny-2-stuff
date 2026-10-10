#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace restoration::platform_api {

// Bungie sends 64-bit ids as JSON strings and hashes as numbers; id() and hash() keep handlers from mixing them up
class JsonWriter final {
public:
    explicit JsonWriter(std::string& output) noexcept : output_(output) {}

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();

    void key(std::string_view name);

    void string(std::string_view value);
    void number(std::int64_t value);
    void number(std::uint64_t value);
    void number(double value);
    void boolean(bool value);
    void null();

    void id(std::uint64_t value);
    void hash(std::uint32_t value);

    void member(std::string_view name, std::string_view value);
    void member(std::string_view name, std::int64_t value);
    void member(std::string_view name, std::uint64_t value);
    void member(std::string_view name, double value);
    void member(std::string_view name, bool value);
    void member_id(std::string_view name, std::uint64_t value);
    void member_hash(std::string_view name, std::uint32_t value);
    void member_null(std::string_view name);

private:
    void separate();
    void raw_string(std::string_view value);

    std::string& output_;
    // opening a container clears it, closing arms it, a member name clears it; so one flag is enough
    bool needComma_{false};
};

} // namespace restoration::platform_api
