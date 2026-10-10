#pragma once
#include "definition.h"
#include "../activity/definition.h"

namespace dawn::state::matchmaking {
[[nodiscard]] bool commit_social_search_locked(const PendingMutation& prepared,ContextSlot& source) noexcept;
// these run under g_stateLock; no descriptor or address is logged or persisted
[[nodiscard]] bool social_world_locked(std::uint64_t account,activity::SessionRecord& world) noexcept;
[[nodiscard]] bool social_world_matches_locked(std::uint64_t account,std::uint64_t session,
                                               std::uint64_t generation,std::uint32_t activityHash) noexcept;
[[nodiscard]] bool prepare_social_search(ContextHandle context,std::uint32_t activity,
    LatestSnapshot& result,PendingMutation& mutation) noexcept;
[[nodiscard]] bool social_search_peers(std::uint64_t first,std::uint64_t second,
                                      std::uint64_t session=0) noexcept;
// a public search never authorizes the target's private fireteam or another owner's
// session
[[nodiscard]] bool social_search_native_lobby(std::uint64_t callerAccount,
    std::uint64_t targetAccount,std::uint64_t targetPlatform,std::uint64_t nativeLobby) noexcept;
}
