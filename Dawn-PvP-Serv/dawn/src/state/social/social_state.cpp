#include <cstring>
#include <cstdio>
#include "social_state.h"

#include <Windows.h>

#include <algorithm>
#include <string_view>

namespace dawn::state::social {
namespace {

SRWLOCK g_lock{SRWLOCK_INIT};
Snapshot g_snapshot{};

template <std::size_t N>
[[nodiscard]] bool valid_text(const std::array<char, N>& text, bool required) noexcept {
    const auto end = std::find(text.cbegin(), text.cend(), '\0');
    if (end == text.cend() || (required && end == text.cbegin())) {
        return false;
    }
    for (auto current = text.cbegin(); current != end; ++current) {
        const unsigned char value = static_cast<unsigned char>(*current);
        if (value < 0x20 || value == 0x7F) {
            return false;
        }
    }
    return true;
}

} // namespace

bool valid(const Snapshot& snapshot) noexcept {
    if (snapshot.localPlatformId == 0 || snapshot.revision == 0
        || snapshot.remoteCount > snapshot.remotes.size()
        || !valid_text(snapshot.localDisplayName, true)) {
        return false;
    }
    for (std::size_t index = 0; index < snapshot.remotes.size(); ++index) {
        const RemoteAccount& remote = snapshot.remotes[index];
        if (index >= snapshot.remoteCount) {
            if (remote.platformId != 0 || remote.accountSoid != 0) {
                return false;
            }
            continue;
        }
        if (remote.platformId == 0 || remote.platformId == snapshot.localPlatformId
            || remote.accountSoid == 0 || remote.relationship == Relationship::none
            || remote.relationship < Relationship::blocked
            || remote.relationship > Relationship::ignoredFriend
            || remote.personaState < 0 || remote.personaState > 6
            || remote.characterClass > 2U || remote.power < 0
            || (remote.joinable && remote.lobbyId == 0)
            || !valid_text(remote.displayName, true) || !valid_text(remote.location, false)
            || !valid_text(remote.steamDisplay, false) || !valid_text(remote.connect, false)
            || !valid_text(remote.playerGroup, false)
            || !valid_text(remote.playerGroupSize, false)) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (snapshot.remotes[prior].platformId == remote.platformId
                || snapshot.remotes[prior].accountSoid == remote.accountSoid) {
                return false;
            }
        }
    }
    return true;
}

bool publish(const Snapshot& snapshot) noexcept {
    if (!valid(snapshot)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    // byte copy, not assignment: past a few KB the optimizer emitted a vectorised loop that
    // faulted in the LTCG build only. Snapshot is trivially copyable, so memcpy is exact
    std::memcpy(&g_snapshot, &snapshot, sizeof g_snapshot);
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

Snapshot snapshot() noexcept {
    AcquireSRWLockShared(&g_lock);
    Snapshot copy;
    std::memcpy(&copy, &g_snapshot, sizeof copy);
    ReleaseSRWLockShared(&g_lock);
    return copy;
}

void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_snapshot = {};
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::state::social
