#include "social_cache.h"
#include "social_delta.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>

#include "../../../client/hooks/external_server/transport.h"
#include "../../../core/identity/device_identity.h"
#include "../../../core/identity/local_ticket.h"
#include "../../../core/logging/log.h"
#include "../../../middleware/social/friend_snapshot_codec.h"
#include "../../../server/bap/runtime.h"
#include "../../../state/social/social_state.h"
#include "../runtime.h"

namespace dawn::steam::runtime::social {
namespace {

constexpr std::uint64_t kRefreshIntervalMilliseconds = 5000;
constexpr std::uint64_t kRetryIntervalMilliseconds = 15000;
constexpr int kPersonaStateChangeCallback = 304;
constexpr int kFriendRichPresenceUpdateCallback = 336;

struct PersonaStateChange final {
    std::uint64_t steamId{};
    std::int32_t changeFlags{};
    std::int32_t padding{};
};
static_assert(sizeof(PersonaStateChange) == 16);

#pragma pack(push, 1)
struct FriendRichPresenceUpdate final {
    std::uint64_t steamId{};
    std::uint32_t appId{};
};
#pragma pack(pop)
static_assert(sizeof(FriendRichPresenceUpdate) == 12);

SRWLOCK g_workerLock{SRWLOCK_INIT};
HANDLE g_worker{};
bool g_accepting{};
bool g_lastRefreshSucceeded{true};
std::uint64_t g_nextRefresh{};

[[nodiscard]] bool same_remote(const state::social::RemoteAccount& left,
                               const state::social::RemoteAccount& right) noexcept {
    return left.platformId == right.platformId && left.accountSoid == right.accountSoid
           && left.selectedCharacterSoid == right.selectedCharacterSoid
           && left.shipInstanceSoid == right.shipInstanceSoid
           && left.lobbyId == right.lobbyId && left.profileRevision == right.profileRevision
           && left.relationship == right.relationship
           && left.personaState == right.personaState && left.appId == right.appId
           && left.shipDefinitionHash == right.shipDefinitionHash
           && left.power == right.power && left.emblemDefinition == right.emblemDefinition
           && left.emblemVariant == right.emblemVariant
           && left.characterLevel == right.characterLevel
           && left.characterClass == right.characterClass && left.joinable == right.joinable
           && left.displayName == right.displayName && left.location == right.location
           && left.steamDisplay == right.steamDisplay && left.connect == right.connect
           && left.playerGroup == right.playerGroup
           && left.playerGroupSize == right.playerGroupSize;
}

[[nodiscard]] bool same_snapshot(const state::social::Snapshot& left,
                                 const state::social::Snapshot& right) noexcept {
    if (left.localPlatformId != right.localPlatformId
        || left.localDisplayName != right.localDisplayName
        || left.remoteCount != right.remoteCount) {
        return false;
    }
    for (std::size_t index = 0; index < left.remoteCount; ++index) {
        if (!same_remote(left.remotes[index], right.remotes[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const state::social::RemoteAccount* find_remote(
    const state::social::Snapshot& snapshot,
    std::uint64_t platformId) noexcept {
    const auto end = snapshot.remotes.cbegin()
                     + static_cast<std::ptrdiff_t>(snapshot.remoteCount);
    const auto remote = std::find_if(
        snapshot.remotes.cbegin(),
        end,
        [platformId](const state::social::RemoteAccount& candidate) noexcept {
            return candidate.platformId == platformId;
        });
    return remote != end ? &*remote : nullptr;
}

void queue_changes(const state::social::Snapshot& before,
                   const state::social::Snapshot& after) noexcept {
    if (before.localDisplayName != after.localDisplayName && after.localPlatformId != 0) {
        const PersonaStateChange localChange{after.localPlatformId, delta::kName, 0};
        (void)steam::queue_callback(
            kPersonaStateChangeCallback, 0, &localChange, sizeof(localChange));
    }
    std::array<std::uint64_t, state::social::kRemoteCapacity * 2> identities{};
    std::size_t count = 0;
    for (std::size_t index = 0; index < before.remoteCount; ++index) {
        identities[count++] = before.remotes[index].platformId;
    }
    for (std::size_t index = 0; index < after.remoteCount; ++index) {
        const std::uint64_t platformId = after.remotes[index].platformId;
        const auto existing = std::find(identities.cbegin(), identities.cbegin() + count, platformId);
        if (existing == identities.cbegin() + count) {
            identities[count++] = platformId;
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint64_t platformId = identities[index];
        if (platformId == 0) {
            continue;
        }
        const state::social::RemoteAccount* const prior = find_remote(before, platformId);
        const state::social::RemoteAccount* const current = find_remote(after, platformId);
        const std::int32_t flags = delta::persona_change_flags(prior, current);
        if (flags != 0) {
            const PersonaStateChange change{platformId, flags, 0};
            (void)steam::queue_callback(
                kPersonaStateChangeCallback, 0, &change, sizeof(change));
        }
        if (delta::rich_presence_changed(prior, current)) {
            const FriendRichPresenceUpdate presence{
                platformId, static_cast<std::uint32_t>(steam::app_id())};
            (void)steam::queue_callback(kFriendRichPresenceUpdateCallback,
                                        0,
                                        &presence,
                                        sizeof(presence));
        }
    }
}

DWORD WINAPI refresh_worker([[maybe_unused]] void* context) noexcept {
    const state::social::Snapshot before = state::social::snapshot();
    const bool success = refresh();
    const state::social::Snapshot after = state::social::snapshot();
    if (success && !same_snapshot(before, after)) {
        queue_changes(before, after);
        server::bap::social_projection_changed();
    }
    AcquireSRWLockExclusive(&g_workerLock);
    g_lastRefreshSucceeded = success;
    ReleaseSRWLockExclusive(&g_workerLock);
    return 0;
}

} // namespace

bool refresh() noexcept {
    std::array<std::byte, middleware::social::friend_snapshot::kMaximumEncodedSize> body{};
    std::array<std::byte, core::identity::device_identity::kProofSize> proof{};
    std::span<const std::byte> requestBody{};
    std::size_t proofSize = 0;
    bool identityReady = false;
    const auto nowValue = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    if constexpr (steam::kUniversalClientBuild) {
        proofSize = core::identity::device_identity::kProofSize;
        identityReady = nowValue > 0
                        && core::identity::device_identity::issue(
                            core::identity::device_identity::Scope::socialSnapshot,
                            static_cast<std::uint64_t>(nowValue),
                            proof);
    } else {
        proofSize = core::identity::local_ticket::kTicketSize;
        identityReady = nowValue > 0
                        && core::identity::local_ticket::issue(
                            steam::local_steam_id(),
                            static_cast<std::uint64_t>(nowValue),
                            std::span<std::byte, core::identity::local_ticket::kTicketSize>(
                                proof.data(), core::identity::local_ticket::kTicketSize));
    }
    requestBody = std::span<const std::byte>(proof).first(proofSize);
    const client::network::HttpRequest request{
        client::network::HttpMethod::post,
        "/v1/social/friends",
        "application/octet-stream",
        {},
        requestBody,
        body,
    };
    client::network::HttpResponse response{};
    state::social::Snapshot snapshot{};
    const bool success = identityReady
                         && client::hooks::external_server::consume(request, response)
                         && response.statusCode == 200
                         && middleware::social::friend_snapshot::decode(
                             std::span<const std::byte>(body).first(response.size), snapshot)
                         && state::social::publish(snapshot);
    SecureZeroMemory(proof.data(), proof.size());
    core::log::write(core::log::Channel::client,
                     success ? core::log::Level::info : core::log::Level::warn,
                     success
                         ? "ev=steam_social stage=refresh result=ok"
                         : "ev=steam_social stage=refresh result=unavailable");
    return success;
}

void activate() noexcept {
    AcquireSRWLockExclusive(&g_workerLock);
    g_accepting = true;
    g_lastRefreshSucceeded = true;
    g_nextRefresh = GetTickCount64() + kRefreshIntervalMilliseconds;
    ReleaseSRWLockExclusive(&g_workerLock);
}

void service(std::uint64_t nowMilliseconds) noexcept {
    HANDLE completed = nullptr;
    AcquireSRWLockExclusive(&g_workerLock);
    if (g_worker != nullptr && WaitForSingleObject(g_worker, 0) == WAIT_OBJECT_0) {
        completed = g_worker;
        g_worker = nullptr;
        if (!g_lastRefreshSucceeded) {
            g_nextRefresh = nowMilliseconds + kRetryIntervalMilliseconds;
        }
    }
    if (g_accepting && g_worker == nullptr && nowMilliseconds >= g_nextRefresh) {
        g_worker = CreateThread(nullptr, 0, &refresh_worker, nullptr, 0, nullptr);
        g_nextRefresh = nowMilliseconds
                        + (g_worker != nullptr ? kRefreshIntervalMilliseconds
                                              : kRetryIntervalMilliseconds);
    }
    ReleaseSRWLockExclusive(&g_workerLock);
    if (completed != nullptr) {
        (void)CloseHandle(completed);
    }
}

void shutdown() noexcept {
    HANDLE worker = nullptr;
    AcquireSRWLockExclusive(&g_workerLock);
    g_accepting = false;
    worker = g_worker;
    ReleaseSRWLockExclusive(&g_workerLock);
    if (worker != nullptr) {
        (void)WaitForSingleObject(worker, INFINITE);
    }
    AcquireSRWLockExclusive(&g_workerLock);
    if (g_worker == worker) {
        g_worker = nullptr;
    }
    g_nextRefresh = 0;
    g_lastRefreshSucceeded = true;
    ReleaseSRWLockExclusive(&g_workerLock);
    if (worker != nullptr) {
        (void)CloseHandle(worker);
    }
}

} // namespace dawn::steam::runtime::social
