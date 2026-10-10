// repairs a file written against an older layout: members whose value form changed
// can't be parsed, so they are replaced with the bundled default before the parse

#include "settings_upgrade.h"

#include <array>
#include <cstdint>
#include <cstdio>

#include "settings.h"

namespace dawn::core::settings::upgrade {
namespace {

constexpr std::string_view kVersionMember = "\"version\"";

struct ReplacedMember {
    std::string_view name;
    std::uint32_t version;
};

constexpr std::array<ReplacedMember, 6> kReplacedMembers{{
    {"\"key_bindings\"", 3},
    {"\"region_private\"", 5},
    {"\"topology\"", 5},
    {"\"characters\"", 5},
    {"\"profile_items\"", 6},
    // version 7 turns the optional in-process route into a mandatory backend origin
    {"\"external_server\"", 7},
}};
constexpr std::size_t kSpliceCapacity = kReplacedMembers.size() + 1;
constexpr std::size_t kVersionTextCapacity = 32;

struct Splice {
    std::size_t start;
    std::size_t end;
    std::string_view text;
};

[[nodiscard]] std::size_t skip_string(std::string_view text, std::size_t position) noexcept {
    for (++position; position < text.size(); ++position) {
        if (text[position] == '\\') {
            ++position;
            continue;
        }
        if (text[position] == '"') {
            return position + 1;
        }
    }
    return text.size();
}

[[nodiscard]] std::size_t skip_value(std::string_view text, std::size_t position) noexcept {
    if (position >= text.size()) {
        return text.size();
    }
    if (text[position] == '"') {
        return skip_string(text, position);
    }
    if (text[position] != '{' && text[position] != '[') {
        std::size_t end = position;
        while (end < text.size() && text[end] != ',' && text[end] != '}' && text[end] != ']') {
            ++end;
        }
        while (end > position
               && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'
                   || text[end - 1] == '\n')) {
            --end;
        }
        return end;
    }
    std::size_t depth = 0;
    for (; position < text.size(); ++position) {
        const char value = text[position];
        if (value == '"') {
            position = skip_string(text, position) - 1;
            continue;
        }
        if (value == '{' || value == '[') {
            ++depth;
            continue;
        }
        if (value == '}' || value == ']') {
            --depth;
            if (depth == 0) {
                return position + 1;
            }
        }
    }
    return text.size();
}

[[nodiscard]] bool value_span(std::string_view text,
                              std::string_view member,
                              std::size_t& start,
                              std::size_t& end) noexcept {
    const std::size_t found = text.find(member);
    if (found == std::string_view::npos) {
        return false;
    }
    std::size_t position = found + member.size();
    const std::string_view blanks = " \t\r\n";
    position = text.find_first_not_of(blanks, position);
    if (position == std::string_view::npos || text[position] != ':') {
        return false;
    }
    position = text.find_first_not_of(blanks, position + 1);
    if (position == std::string_view::npos) {
        return false;
    }
    start = position;
    end = skip_value(text, position);
    return end > start;
}

[[nodiscard]] std::uint32_t document_version(std::string_view document) noexcept {
    std::size_t start = 0;
    std::size_t end = 0;
    if (!value_span(document, kVersionMember, start, end)) {
        return 0;
    }
    std::uint32_t version = 0;
    for (std::size_t position = start; position < end; ++position) {
        const char digit = document[position];
        if (digit < '0' || digit > '9') {
            return 0;
        }
        version = version * 10 + static_cast<std::uint32_t>(digit - '0');
    }
    return version;
}

} // namespace

bool needed(std::string_view document) noexcept {
    return document_version(document) < kSettingsVersion;
}

bool apply(std::string_view document,
           std::string_view bundled,
           std::span<char> output,
           std::size_t& written) noexcept {
    written = 0;
    std::array<Splice, kSpliceCapacity> splices{};
    std::size_t count = 0;

    std::array<char, kVersionTextCapacity> versionText{};
    std::size_t start = 0;
    std::size_t end = 0;
    if (value_span(document, kVersionMember, start, end)) {
        const int length = std::snprintf(
            versionText.data(), versionText.size(), "%u", static_cast<unsigned>(kSettingsVersion));
        if (length <= 0) {
            return false;
        }
        splices[count++] = {start, end, {versionText.data(), static_cast<std::size_t>(length)}};
    } else {
        const std::size_t root = document.find('{');
        if (root == std::string_view::npos) {
            return false;
        }
        const int length = std::snprintf(versionText.data(),
                                         versionText.size(),
                                         "\"version\": %u,",
                                         static_cast<unsigned>(kSettingsVersion));
        if (length <= 0) {
            return false;
        }
        splices[count++] = {
            root + 1, root + 1, {versionText.data(), static_cast<std::size_t>(length)}};
    }

    const std::uint32_t from = document_version(document);
    for (const ReplacedMember& member : kReplacedMembers) {
        // a file at or past that version keeps its own value so a user choice is never lost
        if (from >= member.version) {
            continue;
        }
        std::size_t replacementStart = 0;
        std::size_t replacementEnd = 0;
        if (!value_span(document, member.name, start, end)
            || !value_span(bundled, member.name, replacementStart, replacementEnd)) {
            continue;
        }
        splices[count++] = {
            start, end, bundled.substr(replacementStart, replacementEnd - replacementStart)};
    }

    for (std::size_t index = 1; index < count; ++index) {
        const Splice held = splices[index];
        std::size_t position = index;
        while (position > 0 && splices[position - 1].start > held.start) {
            splices[position] = splices[position - 1];
            --position;
        }
        splices[position] = held;
    }

    std::size_t read = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const Splice& splice = splices[index];
        const std::size_t copied = splice.start - read;
        if (written + copied + splice.text.size() > output.size()) {
            return false;
        }
        document.copy(output.data() + written, copied, read);
        written += copied;
        splice.text.copy(output.data() + written, splice.text.size());
        written += splice.text.size();
        read = splice.end;
    }
    const std::size_t tail = document.size() - read;
    if (written + tail > output.size()) {
        return false;
    }
    document.copy(output.data() + written, tail, read);
    written += tail;
    return true;
}

} // namespace dawn::core::settings::upgrade
