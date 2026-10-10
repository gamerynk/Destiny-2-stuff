#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "../../../core/protocol/activity_player_team.h"

namespace dawn::middleware::bap::activity_message::player_assignment {

// type13 schema 80804F35+28 (object+228); native F9CC60 compares it with the player's
// CodecB byte0 before readiness, so type12's player blob must carry the same assignment
inline constexpr std::uint32_t kAssignment = 0;
inline constexpr std::uint32_t kBias = 1;
inline constexpr std::uint8_t kAuthorityWidth = 5;
inline constexpr std::uint8_t kPlayerWidth = 6;
static_assert(kAssignment + kBias < (1U << kAuthorityWidth));

// native 1700AB0: mode3 + revision1 + CodecB10 + CodecA137 + gear1 + extra1 = 153 bits,
// plus 7 padding, 20 bytes. The old 18-byte blob lacked the assignment and 3 zero flags
inline constexpr std::size_t kBlobMeaningfulBits = 153;
inline constexpr std::size_t kBlobPaddingBits = 7;
inline constexpr std::uint16_t kBlobBytes = 20;
static_assert(kBlobMeaningfulBits + kBlobPaddingBits == kBlobBytes * 8U);

inline constexpr bool competitive_destination(std::string_view name) noexcept {
    return name.starts_with("pvp_");
}
// native faction lookup accepts 0..11; policy semantics still need a live FFA trace
inline constexpr std::int8_t kMaximumCrucibleAssignment = 11;
struct Assignment final {
    std::uint8_t faction{};
    std::uint8_t teamIndex{};
    bool publishRequestedTeam{};
    bool operator==(const Assignment&) const = default;
};
enum class Mode : std::uint8_t { legacy, clash, rumble };
inline constexpr Mode mode_for(std::string_view name) noexcept {
    return name=="pvp_rumble" ? Mode::rumble : name=="pvp_clash" ? Mode::clash : Mode::legacy;
}
// use the native session's accepted side, not join order or account identity; other modes
// and absent/unassigned/transitional pairs keep the proven cooperative encoding
inline constexpr Assignment resolve(bool competitive,
    const core::protocol::ActivityPlayerTeam& team) noexcept {
    if (competitive && team.hasAssignment && team.hasRequestedTeam
        && team.assignment>=0 && team.assignment<=kMaximumCrucibleAssignment
        && team.requestedTeam==team.assignment) {
        const auto index=static_cast<std::uint8_t>(team.assignment);
        return {index,index,true};
    }
    return {};
}

// PRIVATE membership slots are canonical and survive sparse rosters; Rumble has no orbit
// team selection, so combat and scoreboard must use the slot, not the saved fireteam side
inline constexpr Assignment resolve(bool competitive, Mode mode,
    const core::protocol::ActivityPlayerTeam& team, std::size_t memberSlot) noexcept {
    if (!competitive) return {};
    if (mode==Mode::rumble) {
        if (memberSlot>static_cast<std::size_t>(kMaximumCrucibleAssignment)) return {};
        const auto side=static_cast<std::uint8_t>(memberSlot);
        return {side,side,true};
    }
    if (mode==Mode::clash) {
        // a new orbit choice can precede its assignment echo; publishing zero would silently move
        // Bravo to Alpha. Ignore stale multi-team assignments from a prior mode
        const auto side=team.hasRequestedTeam && team.requestedTeam>=0 && team.requestedTeam<2
            ? team.requestedTeam : team.hasAssignment ? team.assignment : -1;
        if (side>=0 && side<2) {
            const auto index=static_cast<std::uint8_t>(side);
            return {index,index,true};
        }
        return {};
    }
    return resolve(competitive,team);
}

} // namespace dawn::middleware::bap::activity_message::player_assignment
