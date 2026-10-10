#pragma once

#include "../../../state/social/definition.h"

#include <cstdint>

namespace dawn::steam::runtime::social::delta {

inline constexpr std::int32_t kName = 0x0001;
inline constexpr std::int32_t kStatus = 0x0002;
inline constexpr std::int32_t kComeOnline = 0x0004;
inline constexpr std::int32_t kGoneOffline = 0x0008;
inline constexpr std::int32_t kGamePlayed = 0x0010;
inline constexpr std::int32_t kRelationship = 0x0200;
inline constexpr std::int32_t kNameFirstSet = 0x0400;

[[nodiscard]] constexpr bool rich_presence_changed(
    const state::social::RemoteAccount* before,
    const state::social::RemoteAccount* after) noexcept {
    return before == nullptr || after == nullptr || before->location != after->location
           || before->steamDisplay != after->steamDisplay || before->connect != after->connect
           || before->playerGroup != after->playerGroup
           || before->playerGroupSize != after->playerGroupSize;
}

[[nodiscard]] constexpr std::int32_t persona_change_flags(
    const state::social::RemoteAccount* before,
    const state::social::RemoteAccount* after) noexcept {
    if (before == nullptr && after == nullptr) {
        return 0;
    }
    if (before == nullptr) {
        std::int32_t flags = kName | kStatus | kRelationship | kNameFirstSet;
        if (after->personaState != 0) {
            flags |= kComeOnline;
        }
        if (after->appId != 0 || after->lobbyId != 0) {
            flags |= kGamePlayed;
        }
        return flags;
    }
    if (after == nullptr) {
        std::int32_t flags = kStatus | kRelationship;
        if (before->personaState != 0) {
            flags |= kGoneOffline;
        }
        if (before->appId != 0 || before->lobbyId != 0) {
            flags |= kGamePlayed;
        }
        return flags;
    }

    std::int32_t flags = 0;
    if (before->displayName != after->displayName) {
        flags |= kName;
        if (before->displayName.front() == '\0' && after->displayName.front() != '\0') {
            flags |= kNameFirstSet;
        }
    }
    if (before->personaState != after->personaState) {
        flags |= kStatus;
        if (before->personaState == 0 && after->personaState != 0) {
            flags |= kComeOnline;
        } else if (before->personaState != 0 && after->personaState == 0) {
            flags |= kGoneOffline;
        }
    }
    if (before->appId != after->appId || before->lobbyId != after->lobbyId) {
        flags |= kGamePlayed;
    }
    if (before->relationship != after->relationship) {
        flags |= kRelationship;
    }
    return flags;
}

} // namespace dawn::steam::runtime::social::delta
