#pragma once

#include <cstdint>
#include <span>

#include "../../../middleware/social/realtime_codec.h"
#include "../endpoint/gameplay_endpoint.h"

namespace dawn::server::gameplay::group::central_lobby_identity {

enum class Result : std::uint8_t {
    resolvedPreferred,
    resolvedOnlyRemaining,
    resolvedOnlyNonOwner,
    invalidLobby,
    invalidBinding,
    noRemainingMember,
    ambiguous,
};

[[nodiscard]] inline bool already_bound(std::span<const std::uint64_t> boundPlatformIds,
                                        std::uint64_t platformId) noexcept {
    if (platformId == 0) {
        return false;
    }
    for (const std::uint64_t bound : boundPlatformIds) {
        if (bound == platformId) {
            return true;
        }
    }
    return false;
}

// more than one unbound member is ambiguous on purpose: arrival order is not authentication
[[nodiscard]] inline Result resolve(
    const middleware::social::realtime::Lobby& lobby,
    std::span<const std::uint64_t> boundPlatformIds,
    std::uint64_t preferredPlatformId,
    std::uint64_t& platformId) noexcept {
    platformId = 0;
    if (lobby.id == 0 || lobby.ownerPlatformId == 0 || lobby.memberCount == 0
        || lobby.memberCount > lobby.members.size()) {
        return Result::invalidLobby;
    }

    bool ownerPresent = false;
    bool preferredPresent = false;
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        const auto& member = lobby.members[index];
        if (member.platformId == 0 || member.accountSoid == 0) {
            return Result::invalidLobby;
        }
        if (member.platformId == lobby.ownerPlatformId) {
            ownerPresent = true;
        }
        if (member.platformId == preferredPlatformId) {
            preferredPresent = true;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (lobby.members[prior].platformId == member.platformId) {
                return Result::invalidLobby;
            }
        }
    }
    if (!ownerPresent) {
        return Result::invalidLobby;
    }

    for (std::size_t boundIndex = 0; boundIndex < boundPlatformIds.size(); ++boundIndex) {
        const std::uint64_t bound = boundPlatformIds[boundIndex];
        bool present = false;
        for (std::size_t memberIndex = 0; memberIndex < lobby.memberCount; ++memberIndex) {
            present = present || lobby.members[memberIndex].platformId == bound;
        }
        if (bound == 0 || !present) {
            return Result::invalidBinding;
        }
        for (std::size_t prior = 0; prior < boundIndex; ++prior) {
            if (boundPlatformIds[prior] == bound) {
                return Result::invalidBinding;
            }
        }
    }

    if (preferredPresent && !already_bound(boundPlatformIds, preferredPlatformId)) {
        platformId = preferredPlatformId;
        return Result::resolvedPreferred;
    }

    std::uint64_t onlyRemaining = 0;
    std::size_t remainingCount = 0;
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        const std::uint64_t candidate = lobby.members[index].platformId;
        if (already_bound(boundPlatformIds, candidate)) {
            continue;
        }
        onlyRemaining = candidate;
        ++remainingCount;
    }
    if (remainingCount == 1) {
        platformId = onlyRemaining;
        return Result::resolvedOnlyRemaining;
    }
    return remainingCount == 0 ? Result::noRemainingMember : Result::ambiguous;
}

// the owner is projected synthetically until its endpoint arrives, so the unique unbound non-owner
// is the joiner; derived from lobby role, not arrival order
[[nodiscard]] inline Result resolve_joiner(
    const middleware::social::realtime::Lobby& lobby,
    std::span<const std::uint64_t> boundPlatformIds,
    std::uint64_t preferredPlatformId,
    std::uint64_t& platformId) noexcept {
    const Result ordinary = resolve(lobby, boundPlatformIds, preferredPlatformId, platformId);
    if (ordinary != Result::ambiguous) {
        return ordinary;
    }

    std::uint64_t onlyNonOwner = 0;
    std::size_t nonOwnerCount = 0;
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        const std::uint64_t candidate = lobby.members[index].platformId;
        if (candidate == lobby.ownerPlatformId || already_bound(boundPlatformIds, candidate)) {
            continue;
        }
        onlyNonOwner = candidate;
        ++nonOwnerCount;
    }
    if (nonOwnerCount == 1) {
        platformId = onlyNonOwner;
        return Result::resolvedOnlyNonOwner;
    }
    platformId = 0;
    return Result::ambiguous;
}

// the endpoint publishes one invertible machine id per lobby; this bridges a native join (no
// platform id) to social identity
[[nodiscard]] inline Result resolve_for_session(
    std::uint64_t endpointMachineId,
    std::uint64_t gameplayMachineId,
    const middleware::social::realtime::Lobby& lobby,
    std::span<const std::uint64_t> boundPlatformIds,
    std::uint64_t preferredPlatformId,
    std::uint64_t& platformId) noexcept {
    platformId = 0;
    const std::uint64_t recoveredLobbyId =
        endpoint::recover_lobby_id(endpointMachineId, gameplayMachineId);
    if (recoveredLobbyId == 0 || lobby.id == 0 || recoveredLobbyId != lobby.id) {
        return Result::invalidLobby;
    }
    return resolve(lobby, boundPlatformIds, preferredPlatformId, platformId);
}

[[nodiscard]] inline Result resolve_joiner_for_session(
    std::uint64_t endpointMachineId,
    std::uint64_t gameplayMachineId,
    const middleware::social::realtime::Lobby& lobby,
    std::span<const std::uint64_t> boundPlatformIds,
    std::uint64_t preferredPlatformId,
    std::uint64_t& platformId) noexcept {
    platformId = 0;
    const std::uint64_t recoveredLobbyId =
        endpoint::recover_lobby_id(endpointMachineId, gameplayMachineId);
    if (recoveredLobbyId == 0 || lobby.id == 0 || recoveredLobbyId != lobby.id) {
        return Result::invalidLobby;
    }
    return resolve_joiner(lobby, boundPlatformIds, preferredPlatformId, platformId);
}

[[nodiscard]] inline const char* name(Result result) noexcept {
    switch (result) {
    case Result::resolvedPreferred:
        return "preferred";
    case Result::resolvedOnlyRemaining:
        return "only_remaining";
    case Result::resolvedOnlyNonOwner:
        return "only_non_owner";
    case Result::invalidLobby:
        return "invalid_lobby";
    case Result::invalidBinding:
        return "invalid_binding";
    case Result::noRemainingMember:
        return "no_remaining_member";
    case Result::ambiguous:
        return "ambiguous";
    }
    return "unknown";
}

} // namespace dawn::server::gameplay::group::central_lobby_identity
