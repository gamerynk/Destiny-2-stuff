#include "remote_profile_cache.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstring>
#include <vector>

#include "../../../../RestorationServer/src/account_codec.h"
#include "../../client/hooks/external_server/transport.h"
#include "../../core/identity/device_identity.h"
#include "../../core/identity/local_ticket.h"
#include "../../core/logging/log.h"
#include "../../steam/runtime/runtime.h"
#include "../runtime/runtime.h"
#include "social_state.h"

namespace dawn::state::social::remote_profile_cache {
namespace {

constexpr std::size_t kMaximumProfileSize = 2 * 1024 * 1024;

struct Entry final {
    AccountState account{};
    std::uint64_t platformId{};
    std::uint64_t revision{};
    bool occupied{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Entry, kProfileCacheCapacity> g_entries{};

[[nodiscard]] bool target(std::uint64_t accountSoid,
                          std::uint64_t& platformId,
                          std::uint64_t& revision) noexcept {
    const Snapshot current = snapshot();
    for (std::size_t index = 0; index < current.remoteCount; ++index) {
        const RemoteAccount& remote = current.remotes[index];
        if (remote.accountSoid == accountSoid
            && remote.relationship == Relationship::friendAccount) {
            platformId = remote.platformId;
            revision = remote.profileRevision;
            return platformId != 0 && revision != 0;
        }
    }
    return false;
}

[[nodiscard]] bool download(std::uint64_t platformId,
                            std::uint64_t expectedRevision,
                            AccountState& output,
                            std::uint64_t& revision) noexcept {
    constexpr std::size_t kMaximumProofSize = core::identity::device_identity::kProofSize;
    std::array<std::byte, kMaximumProofSize + sizeof platformId> requestStorage{};
    const auto nowValue = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    std::size_t proofSize = 0;
    bool identityReady = false;
    if constexpr (steam::kUniversalClientBuild) {
        proofSize = core::identity::device_identity::kProofSize;
        identityReady = nowValue > 0
                        && core::identity::device_identity::issue(
                            core::identity::device_identity::Scope::socialProfile,
                            static_cast<std::uint64_t>(nowValue),
                            std::span<std::byte, core::identity::device_identity::kProofSize>(
                                requestStorage.data(),
                                core::identity::device_identity::kProofSize));
    } else {
        proofSize = core::identity::local_ticket::kTicketSize;
        identityReady = nowValue > 0
                        && core::identity::local_ticket::issue(
                            steam::local_steam_id(),
                            static_cast<std::uint64_t>(nowValue),
                            std::span<std::byte, core::identity::local_ticket::kTicketSize>(
                                requestStorage.data(),
                                core::identity::local_ticket::kTicketSize));
    }
    if (!identityReady) {
        return false;
    }
    std::memcpy(requestStorage.data() + proofSize, &platformId, sizeof platformId);
    std::vector<std::byte> responseStorage(kMaximumProfileSize);
    const client::network::HttpRequest request{
        client::network::HttpMethod::post,
        "/v1/social/profile",
        "application/octet-stream",
        {},
        std::span<const std::byte>(requestStorage).first(proofSize + sizeof platformId),
        responseStorage,
    };
    client::network::HttpResponse response{};
    const bool transported = client::hooks::external_server::consume(request, response)
                             && response.statusCode == 200
                             && response.size > sizeof revision
                             && response.size <= responseStorage.size();
    SecureZeroMemory(requestStorage.data(), requestStorage.size());
    if (!transported) {
        return false;
    }
    std::memcpy(&revision, responseStorage.data(), sizeof revision);
    const bool decoded = revision >= expectedRevision
                         && restoration::account_codec::decode(
                             std::span<const std::byte>(responseStorage)
                                 .subspan(sizeof revision, response.size - sizeof revision),
                             output);
    SecureZeroMemory(responseStorage.data(), responseStorage.size());
    return decoded;
}

} // namespace

bool resolve(std::uint64_t accountSoid, AccountState& output) noexcept {
    output = {};
    const std::uint64_t viewerAccountSoid = state::account_snapshot().primarySoid;
    if (state::resolve_friend_profile(viewerAccountSoid, accountSoid, output)) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=remote_profile stage=resolve result=server_store");
        return true;
    }
    std::uint64_t platformId = 0;
    std::uint64_t expectedRevision = 0;
    if (accountSoid == 0 || !target(accountSoid, platformId, expectedRevision)) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    for (const Entry& entry : g_entries) {
        if (entry.occupied && entry.platformId == platformId
            && entry.revision >= expectedRevision) {
            output = entry.account;
            ReleaseSRWLockShared(&g_lock);
            return output.primarySoid == accountSoid && account::valid(output);
        }
    }
    ReleaseSRWLockShared(&g_lock);

    AccountState downloaded{};
    std::uint64_t revision = 0;
    if (!download(platformId, expectedRevision, downloaded, revision)
        || downloaded.primarySoid != accountSoid || !account::valid(downloaded)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=remote_profile stage=resolve result=unavailable");
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    Entry* destination = nullptr;
    for (Entry& entry : g_entries) {
        if (entry.occupied && entry.platformId == platformId) {
            destination = &entry;
            break;
        }
        if (!entry.occupied && destination == nullptr) {
            destination = &entry;
        }
    }
    if (destination != nullptr) {
        destination->account = downloaded;
        destination->platformId = platformId;
        destination->revision = revision;
        destination->occupied = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    output = downloaded;
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=remote_profile stage=resolve result=ok");
    return destination != nullptr;
}

void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_entries = {};
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::state::social::remote_profile_cache
