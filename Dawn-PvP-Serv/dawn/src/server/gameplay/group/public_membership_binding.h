#pragma once
#include <cstdint>
namespace dawn::server::gameplay::group::public_membership_binding {
// claim() keeps failed/retrying admissions, so occupancy alone isn't authority to publish a
// public-region peer: its machine must come from the exact authenticated PRIVATE participant
template<class Record>
[[nodiscard]] constexpr bool ready(const Record& row) noexcept {
    return row.occupied && row.sessionId && row.joiningPlatformId && row.activityMemberKey
        && row.activityAccount && row.activitySource && row.activityCreatedRevision
        && row.activityJoinedRevision && row.activityPlayerId && row.activityPlayerId!=UINT64_MAX;
}
struct SourceFence {
    std::uint64_t account{},source{},member{},created{},joined{},player{};
    bool valid{};
};
template<class Record>
[[nodiscard]] constexpr bool current(const Record& row,const SourceFence& source) noexcept {
    return ready(row) && source.valid && row.activityPlayerId
        && row.activityPlayerId!=UINT64_MAX && row.activityAccount==source.account
        && row.activitySource==source.source && row.activityMemberKey==source.member
        && row.activityCreatedRevision==source.created && row.activityJoinedRevision==source.joined
        && row.activityPlayerId==source.player;
}
}
