#pragma once

#include "../../../middleware/social/realtime_codec.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace dawn::steam::runtime::social::lobby_cache {
namespace wire = middleware::social::realtime;

[[nodiscard]] inline bool contains(const wire::Lobby& lobby, std::uint64_t member) noexcept {
    const auto end = lobby.members.begin() + (std::min)(lobby.memberCount, lobby.members.size());
    return member != 0 && std::any_of(lobby.members.begin(), end,
        [member](const wire::Member& row) { return row.platformId == member; });
}

// Steam allows one regular and two invisible memberships; never evict a live session
class Cache final {
    struct Entry { wire::Lobby lobby{}; bool fresh{}; };
    std::array<Entry, 3> entries_{};
public:
    [[nodiscard]] bool can_remember(const wire::Lobby& lobby) const noexcept {
        if (!lobby.id || lobby.memberCount > lobby.members.size()
            || lobby.metadataCount > lobby.metadata.size()) return false;
        for (const auto& entry : entries_)
            if (!entry.lobby.id || entry.lobby.id == lobby.id) return true;
        return false;
    }
    [[nodiscard]] std::array<std::uint64_t,3> retained_ids() const noexcept {
        return {entries_[0].lobby.id,entries_[1].lobby.id,entries_[2].lobby.id};
    }
    void remember(const wire::Lobby& lobby) noexcept {
        if (lobby.id == 0 || lobby.memberCount > lobby.members.size()
            || lobby.metadataCount > lobby.metadata.size()) return;
        for (auto& entry : entries_) {
            if (entry.lobby.id == lobby.id) { entry = {lobby, true}; return; }
        }
        for (auto& entry : entries_) {
            if (entry.lobby.id == 0) { entry = {lobby, true}; return; }
        }
    }
    void forget(std::uint64_t id) noexcept {
        for (auto& entry : entries_) if (entry.lobby.id == id) entry = {};
    }
    void forget_other_regular(std::uint64_t id) noexcept {
        for (auto& entry : entries_)
            if (entry.lobby.type != 3 && entry.lobby.id != id) entry = {};
    }
    void invalidate(std::uint64_t id) noexcept {
        for (auto& entry : entries_) if (entry.lobby.id == id) entry.fresh = false;
    }
    [[nodiscard]] bool needs_refresh(std::uint64_t id) const noexcept {
        for (const auto& entry : entries_)
            if (id != 0 && entry.lobby.id == id) return !entry.fresh;
        return false; // an unknown friend's lobby is not a local membership
    }
    [[nodiscard]] wire::Lobby snapshot(std::uint64_t id) const noexcept {
        for (const auto& entry : entries_)
            if (id != 0 && entry.lobby.id == id && entry.fresh) return entry.lobby;
        return {};
    }
    [[nodiscard]] bool owned(std::uint64_t id, std::uint64_t member) const noexcept {
        for (const auto& entry : entries_)
            if (id != 0 && entry.lobby.id == id && entry.fresh)
                return entry.lobby.ownerPlatformId == member && contains(entry.lobby, member);
        return false;
    }
};

[[nodiscard]] inline std::uint32_t member_change(const wire::Event& event,
                                               const wire::Lobby& sameLobby) noexcept {
    std::uint32_t change{};
    if (event.payloadSize == sizeof(change)) {
        std::memcpy(&change, event.payload.data(), sizeof(change));
        if (change == 1U || change == 2U || change == 4U) return change;
    }
    return sameLobby.id == event.lobbyId && contains(sameLobby, event.memberPlatformId)
        ? 1U : 2U;
}

[[nodiscard]] constexpr bool returns_operation_lobby(wire::Operation operation,
                                                    int result) noexcept {
    return result == 1 && (operation == wire::Operation::createLobby
                          || operation == wire::Operation::joinLobby);
}

// apply only explicit authoritative departures; a newer returned membership wins over an
// older queued LEFT, and unrelated peer departures never evict us
inline void retire_response_departures(Cache& cache,const wire::Response& response,
    const wire::Lobby* selected,std::uint64_t local) noexcept {
    if(response.eventCount>response.events.size())return;
    for(std::size_t i=0;i<response.eventCount;++i){
        const auto& e=response.events[i];std::uint32_t change{};
        if(e.payloadSize==sizeof(change))std::memcpy(&change,e.payload.data(),sizeof(change));
        const bool departed=e.kind==wire::EventKind::lobbyDestroyed
            || (local && e.kind==wire::EventKind::memberChanged && e.memberPlatformId==local
                && (change==2U || change==4U));
        const bool current=(response.lobby.id==e.lobbyId && contains(response.lobby,local))
            || (selected && selected->id==e.lobbyId && contains(*selected,local));
        if(departed && !current)cache.forget(e.lobbyId);
    }
}

inline void store_response(Cache& cache,const wire::Response& response,
    const wire::Lobby* selected,std::uint64_t local) noexcept {
    // Create/Join can retire an old invisible session in the same response; release that slot
    // first, or a full cache drops the target and GetLobbyOwner/Data is empty in LobbyEnter
    retire_response_departures(cache,response,selected,local);
    cache.remember(response.lobby);
    if(selected)cache.remember(*selected);
}

[[nodiscard]] inline bool authoritative_absence(const wire::Request& query,
    const wire::Response& response) noexcept {
    // QueryLobby has no side effects and does not drain events; a transport error or generic
    // failure is never permission to evict a live entry
    return query.operation==wire::Operation::queryLobby && query.lobbyId!=0
        && response.result==15 && response.lobby.id==0 && response.eventCount==0
        && response.cursor==query.cursor;
}
} // namespace dawn::steam::runtime::social::lobby_cache
