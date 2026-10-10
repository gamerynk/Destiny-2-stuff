#include "steam/runtime/social/lobby_cache.h"
#include <cstdio>

namespace cache = dawn::steam::runtime::social::lobby_cache;
namespace wire = dawn::middleware::social::realtime;
int main() {
    static cache::Cache cache;
    unsigned checks = 0;
    const auto check = [&checks](bool ok, const char* label) {
        ++checks;
        if (!ok) std::printf("FAIL: lobby cache: %s\n", label);
        return ok;
    };
    wire::Lobby fireteam{};
    fireteam.id = 11; fireteam.ownerPlatformId = 101; fireteam.type = 3;
    fireteam.members[0].platformId = 101; fireteam.memberCount = 1;
    wire::Lobby activity = fireteam; activity.id = 12;
    activity.members[1].platformId = 102; activity.memberCount = 2;
    wire::Lobby regular = fireteam; regular.id = 13; regular.type = 1;
    cache.remember(fireteam); cache.remember(activity); cache.remember(regular);
    if (!check(cache.snapshot(11).memberCount == 1 && cache.snapshot(12).memberCount == 2
        && cache.snapshot(13).id == 13, "three distinct memberships retained")) return 1;
    auto overflow = regular; overflow.id = 14; cache.remember(overflow);
    if (!check(cache.snapshot(14).id == 0 && cache.snapshot(11).id == 11,
        "capacity never evicts a live fireteam")) return 1;
    cache.invalidate(12);
    if (!check(cache.needs_refresh(12) && cache.snapshot(12).id == 0
        && !cache.needs_refresh(11) && cache.owned(11,101)
        && !cache.owned(12,101), "invalidating activity does not erase fireteam")) return 1;
    if (!check(!cache.needs_refresh(999) && !cache.needs_refresh(0),
        "unknown lobbies cannot trigger speculative HTTP queries")) return 1;
    cache.remember(activity);
    if (!check(!cache.needs_refresh(12) && cache.snapshot(12).memberCount == 2,
        "authenticated per-ID response refreshes only that entry")) return 1;
    cache.forget(12); cache.remember(overflow);
    if (!check(cache.snapshot(12).id == 0 && !cache.needs_refresh(12)
        && cache.snapshot(14).id == 14 && cache.owned(11,101), "leave releases exact slot")) return 1;
    if (!check(!cache.owned(11,102) && !cache.owned(11,0), "publication requires actual owner")) return 1;
    auto bad = fireteam; bad.memberCount = bad.members.size() + 1;
    cache.remember(bad);
    if (!check(cache.snapshot(11).memberCount == 1, "malformed response cannot replace cache")) return 1;
    wire::Event event{}; event.kind = wire::EventKind::memberChanged;
    event.lobbyId = 12; event.memberPlatformId = 102;
    std::uint32_t entered = 1; event.payloadSize = sizeof(entered);
    std::memcpy(event.payload.data(), &entered, sizeof(entered));
    if (!check(cache::member_change(event, fireteam) == 1,
        "activity entry callback uses event, not unrelated solo fireteam")) return 1;
    std::uint32_t left = 2; std::memcpy(event.payload.data(), &left, sizeof(left));
    if (!check(cache::member_change(event, activity) == 2,
        "departure callback beats older retained membership")) return 1;
    event.payloadSize = 0;
    if (!check(cache::member_change(event, activity) == 1
        && cache::member_change(event, fireteam) == 2, "legacy fallback is exact-lobby only")) return 1;
    for (const auto op : {wire::Operation::createLobby, wire::Operation::joinLobby}) {
        if (!check(cache::returns_operation_lobby(op,1)
            && !cache::returns_operation_lobby(op,15), "successful mutation needs selected query")) return 1;
    }
    for (const auto op : {wire::Operation::poll, wire::Operation::queryLobby,
                          wire::Operation::leaveLobby, wire::Operation::publishNativeFireteamDescriptor}) {
        if (!check(!cache::returns_operation_lobby(op,1), "non-create/join cannot replay mutation")) return 1;
    }
    cache.forget_other_regular(13);
    if (!check(cache.snapshot(14).id == 0 && cache.owned(11,101),
        "legacy regular lobby replacement preserves invisible sessions")) return 1;
    cache = {};
    if (!check(cache.snapshot(11).id == 0 && !cache.needs_refresh(11), "shutdown clears membership cache")) return 1;
    std::printf("PASS: %u per-ID lobby cache checks\n", checks);
    return 0;
}
