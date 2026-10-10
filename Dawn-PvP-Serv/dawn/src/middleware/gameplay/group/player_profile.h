#pragma once

#include <array>
#include <cstdint>

namespace dawn::middleware::gameplay::group {

// supplied by the endpoint's ID-34 player-add. Consumers use them as a per-account association
// key, so rebuilding the profile with zeros makes distinct rows resolve to one contact model
struct PlayerProfileSupplement {
    bool field2Present{};
    std::uint16_t field2{};
    bool field3Present{};
    std::uint8_t field3{};
    bool field8Present{};
    std::uint8_t field8{};
    bool field4Present{};
    std::array<std::int32_t, 2> field4{};
    bool field5Present{};
    std::array<std::uint64_t, 2> field5{};
    bool field6Present{};
    std::uint32_t field6FloatBits{};
    std::array<std::int16_t, 4> field6Signed{};
    std::uint8_t field6Byte{};
    bool field7Present{};
    std::uint32_t field7{};

    std::array<std::uint32_t, 3> tailWords{};
    std::uint8_t tailFive{};
    std::uint8_t tailTwo{};
    bool tailFlag{};
    bool tailOptionalPresent{};
    std::uint8_t tailOptionalFive{};

    [[nodiscard]] constexpr bool operator==(const PlayerProfileSupplement&) const noexcept =
        default;
};

// retail's first two-player ID-30 can precede the endpoint's remove/add handoff; both rows
// still need distinct association keys or roster contacts bind to the same cached account
[[nodiscard]] constexpr PlayerProfileSupplement make_account_membership_profile_supplement(
    std::uint64_t primarySoid,
    std::uint64_t presentedCharacterSoid,
    std::uint32_t lightFloatBits) noexcept {
    PlayerProfileSupplement profile{};
    profile.field2Present = true;
    profile.field2 = 0xBD;
    profile.field3Present = true;
    profile.field3 = 0xFF;
    profile.field8Present = true;
    profile.field8 = 0xFF;
    profile.field4Present = true;
    profile.field4 = {0, 0};
    profile.field5Present = true;
    profile.field5 = {primarySoid, presentedCharacterSoid};
    profile.field6Present = true;
    profile.field6FloatBits = lightFloatBits;
    profile.field6Signed = {-1, -1, -1, -1};
    profile.field6Byte = 0;
    profile.field7Present = true;
    profile.field7 = 12;
    profile.tailFive = 1;
    profile.tailTwo = 1;
    return profile;
}

} // namespace dawn::middleware::gameplay::group
