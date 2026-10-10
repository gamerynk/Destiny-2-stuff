#pragma once

#include <cstdint>

namespace dawn::steam::interfaces::methods::matchmaking_policy {

inline constexpr std::uint64_t kCentralLobbyIdPrefix = 0x0184000000000000ULL;
inline constexpr std::uint64_t kCentralLobbyIdMask = 0xFFFFFFFF00000000ULL;

// public-region join descriptors carry server-run online-session ids that are not Steam
// lobby accounts; Destiny still passes them to ISteamMatchmaking, so they stay local
[[nodiscard]] constexpr bool central_lobby_id(std::uint64_t lobby) noexcept {
    return (lobby & kCentralLobbyIdMask) == kCentralLobbyIdPrefix;
}

} // namespace dawn::steam::interfaces::methods::matchmaking_policy
