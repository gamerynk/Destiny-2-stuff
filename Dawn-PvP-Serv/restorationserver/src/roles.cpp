#include "roles.h"

#include <algorithm>
#include <cstddef>

namespace restoration::roles {
namespace {

struct CapabilityName final {
    std::uint32_t bit;
    std::string_view name;
};

constexpr std::array<CapabilityName, 5> kCapabilityNames{{
    {kChannelTesting, "channel.testing"},
    {kChannelPrerelease, "channel.prerelease"},
    {kOffline, "offline"},
    {kSkipVerify, "depot.skip_verify"},
    {kIssueCodes, "codes.issue"},
}};

} // namespace

std::uint32_t capabilities(std::uint32_t roleMask) noexcept {
    std::uint32_t result = 0;
    for (std::size_t index = 0; index < kRoles.size(); ++index) {
        if ((roleMask & (1U << index)) != 0) {
            result |= kRoles[index].capabilities;
        }
    }
    return result;
}

std::uint32_t grant_from_name(std::string_view name) noexcept {
    if (name == "testing") return kChannelTesting;
    if (name == "prerelease") return kChannelPrerelease;
    if (name == "offline") return kOffline;
    return 0;
}

void append_json_string(std::string& output, std::string_view value) {
    output.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character >= 0x20U) output.push_back(static_cast<char>(character));
            break;
        }
    }
    output.push_back('"');
}

void append_badges_json(std::uint32_t roleMask, std::string& output) {
    std::array<std::size_t, kRoles.size()> held{};
    std::size_t count = 0;
    for (std::size_t index = 0; index < kRoles.size(); ++index) {
        if ((roleMask & (1U << index)) != 0) {
            held[count++] = index;
        }
    }
    std::sort(held.begin(), held.begin() + static_cast<std::ptrdiff_t>(count), [](std::size_t left, std::size_t right) {
        return kRoles[left].displayOrder < kRoles[right].displayOrder;
    });
    output.push_back('[');
    for (std::size_t index = 0; index < count; ++index) {
        const Role& role = kRoles[held[index]];
        if (index != 0) output.push_back(',');
        output += "{\"id\":";
        append_json_string(output, role.key);
        output += ",\"label\":";
        append_json_string(output, role.label);
        output += ",\"color\":";
        append_json_string(output, role.color);
        output.push_back('}');
    }
    output.push_back(']');
}

void append_capabilities_json(std::uint32_t capabilities, std::string& output) {
    output.push_back('[');
    bool first = true;
    for (const CapabilityName& entry : kCapabilityNames) {
        if ((capabilities & entry.bit) == 0) continue;
        if (!first) output.push_back(',');
        append_json_string(output, entry.name);
        first = false;
    }
    output.push_back(']');
}

void append_role_ids_json(std::string& output) {
    output += "{\"roles\":[";
    for (std::size_t index = 0; index < kRoles.size(); ++index) {
        if (index != 0) output.push_back(',');
        append_json_string(output, kRoles[index].id);
    }
    output += "]}";
}

} // namespace restoration::roles
