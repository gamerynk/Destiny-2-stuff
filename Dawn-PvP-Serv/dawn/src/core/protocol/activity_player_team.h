#pragma once
#include <cstdint>

namespace dawn::core::protocol {
struct ActivityPlayerTeam final {
    std::int8_t assignment{-1};
    std::int8_t requestedTeam{-1};
    bool hasAssignment{};
    bool hasRequestedTeam{};
    bool operator==(const ActivityPlayerTeam&) const = default;
};

inline ActivityPlayerTeam merge_player_team(ActivityPlayerTeam value,
                                            const ActivityPlayerTeam& delta) noexcept {
    if (delta.hasAssignment) {
        value.assignment=delta.assignment;
        value.hasAssignment=true;
    }
    if (delta.hasRequestedTeam) {
        value.requestedTeam=delta.requestedTeam;
        value.hasRequestedTeam=true;
    }
    return value;
}
}
