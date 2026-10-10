#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// the bot mirrors member roles as a bit mask over kRoles; every privileged request is
// re-checked against the stored mask, so a modified launcher can't change what it may do
namespace restoration::roles {

enum Capability : std::uint32_t {
    kChannelTesting = 1U << 0,
    kChannelPrerelease = 1U << 1,
    kOffline = 1U << 2,
    kSkipVerify = 1U << 3,
    kIssueCodes = 1U << 4,
};

inline constexpr std::uint32_t kEarlyAccess = kChannelTesting | kChannelPrerelease;
inline constexpr std::uint32_t kDeveloper = kEarlyAccess | kOffline | kSkipVerify;
inline constexpr std::uint32_t kOwner = kDeveloper | kIssueCodes;
inline constexpr std::uint32_t kGrantable = kEarlyAccess | kOffline;

struct Role final {
    std::string_view id;
    std::string_view key;
    std::string_view label;
    std::string_view color;
    std::uint32_t capabilities;
    int displayOrder;
};

// bit i of a stored mask is kRoles[i]; append only, reordering reassigns every stored mask
inline constexpr std::array<Role, 7> kRoles{{
    {"1545869519807979520", "override", "Override", "#9aa3b5", kOwner, 1},
    {"1546714644276711524", "praetorian", "Praetorian", "#9d7bff", kOwner, 0},
    {"1548310219090825317", "architect", "Architect", "#4f7fd8", kDeveloper, 2},
    {"1542632126380253275", "developer", "Developer", "#5aa0ff", kDeveloper, 3},
    {"1547755152054689853", "creator", "Content Creator", "#ff6b6b", kEarlyAccess, 4},
    {"1547013864498139269", "tester", "Early Tester", "#e889c8", kEarlyAccess, 5},
    {"1546762329050320946", "arg", "ARG", "#6fd6b8", 0, 6},
}};

inline constexpr std::uint64_t kMaskLimit = 1ULL << kRoles.size();

[[nodiscard]] std::uint32_t capabilities(std::uint32_t roleMask) noexcept;

[[nodiscard]] std::uint32_t grant_from_name(std::string_view name) noexcept;

void append_json_string(std::string& output, std::string_view value);

void append_badges_json(std::uint32_t roleMask, std::string& output);

void append_capabilities_json(std::uint32_t capabilities, std::string& output);

void append_role_ids_json(std::string& output);

} // namespace restoration::roles
