#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../../state/social/social_state.h"
#include "../../runtime/social/realtime_client.h"
#include "../internal.h"
#include "matchmaking_policy.h"

namespace dawn::steam::interfaces::methods {
namespace {

namespace realtime = runtime::social::realtime_client;
namespace wire = middleware::social::realtime;

constexpr int kLobbyEnterCallback = 504;
constexpr int kLobbyDataUpdateCallback = 505;
constexpr int kLobbyCreatedCallback = 513;
constexpr int kResultOk = 1;
constexpr int kResultFail = 2;
constexpr DWORD kLobbyEnterSuccess = 1;
constexpr DWORD kLobbyEnterFailure = 2;
constexpr unsigned kRemoteQueryReportLimit = 96;
constexpr std::size_t kCurrentLobbyObservationCapacity = 128;

std::atomic_uint g_remoteQueryReportCount{};
std::array<std::atomic<std::uint64_t>, kCurrentLobbyObservationCapacity>
    g_currentLobbyObservations{};

struct LobbyEnter final {
    std::uint64_t lobby{};
    DWORD permissions{};
    bool locked{};
    std::array<std::byte, 3> padding{};
    DWORD response{};
};

struct LobbyCreated final {
    int result{};
    DWORD padding{};
    std::uint64_t lobby{};
};

struct LobbyDataUpdate final {
    std::uint64_t lobby{};
    std::uint64_t member{};
    std::uint8_t success{};
    std::array<std::byte, 7> padding{};
};

static_assert(sizeof(LobbyEnter) == 24);
static_assert(sizeof(LobbyCreated) == 16);
static_assert(sizeof(LobbyDataUpdate) == 24);

[[nodiscard]] bool copy_text(const char* source, char* destination, int capacity) noexcept {
    if (source == nullptr || destination == nullptr || capacity <= 0) {
        return false;
    }
    const std::size_t length = std::strlen(source);
    if (length >= static_cast<std::size_t>(capacity)) {
        destination[0] = '\0';
        return false;
    }
    std::memcpy(destination, source, length + 1U);
    return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool assign_text(std::array<char, Capacity>& destination,
                               const char* source) noexcept {
    destination = {};
    if (source == nullptr) {
        return false;
    }
    const std::size_t length = std::strlen(source);
    if (length == 0 || length >= destination.size()) {
        return false;
    }
    std::memcpy(destination.data(), source, length + 1U);
    return true;
}

[[nodiscard]] const wire::Metadata* metadata(const wire::Lobby& lobby,
                                             const char* key) noexcept {
    if (key == nullptr || *key == '\0') {
        return nullptr;
    }
    for (std::size_t index = 0; index < lobby.metadataCount; ++index) {
        if (std::strcmp(lobby.metadata[index].key.data(), key) == 0) {
            return &lobby.metadata[index];
        }
    }
    return nullptr;
}

[[nodiscard]] const wire::Member* member(const wire::Lobby& lobby,
                                         std::uint64_t platformId) noexcept {
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        if (lobby.members[index].platformId == platformId) {
            return &lobby.members[index];
        }
    }
    return nullptr;
}

[[nodiscard]] const wire::Metadata* member_metadata(const wire::Lobby& lobby,
                                                    std::uint64_t platformId,
                                                    const char* key) noexcept {
    const wire::Member* const found = member(lobby, platformId);
    if (found == nullptr || key == nullptr || *key == '\0') {
        return nullptr;
    }
    for (std::size_t index = 0; index < found->metadataCount; ++index) {
        if (std::strcmp(found->metadata[index].key.data(), key) == 0) {
            return &found->metadata[index];
        }
    }
    return nullptr;
}

// the realtime cache only owns the lobby this install joined, but SteamMatchmaking must
// still answer capacity/owner queries for the GetFriendGamePlayed lobby before JoinLobby
[[nodiscard]] bool remote_lobby(std::uint64_t lobbyId,
                                state::social::RemoteAccount& output) noexcept {
    output = {};
    if (lobbyId == 0) {
        return false;
    }
    const state::social::Snapshot social = state::social::snapshot();
    for (std::size_t index = 0; index < social.remoteCount; ++index) {
        const state::social::RemoteAccount& remote = social.remotes[index];
        if (remote.lobbyId == lobbyId
            && remote.relationship == state::social::Relationship::friendAccount
            && remote.platformId != 0) {
            output = remote;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool execute_ok(wire::Request request, wire::Response& response) noexcept {
    return realtime::execute(request, response) && response.result == kResultOk;
}

[[nodiscard]] bool queue_data_update(std::uint64_t lobby,
                                     std::uint64_t memberId,
                                     bool success = true) noexcept {
    const LobbyDataUpdate callback{lobby, memberId, success ? 1U : 0U, {}};
    return queue_callback(kLobbyDataUpdateCallback, 0, &callback, sizeof(callback));
}

void report(const char* action,
            std::uint64_t lobby,
            std::uint64_t memberId,
            bool success) noexcept {
    std::array<char, 192> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=steam stage=lobby action=%s result=%s lobby=0x%016llX"
                                      " member=0x%016llX",
                                      action,
                                      success ? "ok" : "fail",
                                      static_cast<unsigned long long>(lobby),
                                      static_cast<unsigned long long>(memberId));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         success ? core::log::Level::info : core::log::Level::warn,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

void report_remote_query(const char* action,
                         std::uint64_t lobby,
                         long long value,
                         const char* key = nullptr) noexcept {
    const unsigned reportIndex = g_remoteQueryReportCount.fetch_add(1, std::memory_order_relaxed);
    if (reportIndex >= kRemoteQueryReportLimit) {
        return;
    }
    std::array<char, 320> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=steam stage=lobby_prejoin query=%s lobby=0x%016llX value=%lld key=%s n=%u",
        action != nullptr ? action : "unknown",
        static_cast<unsigned long long>(lobby),
        value,
        key != nullptr ? key : "-",
        reportIndex + 1U);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

[[nodiscard]] std::uint64_t current_lobby_observation_hash(
    const char* action,
    std::uint64_t requestedLobby,
    const wire::Lobby& current,
    int memberIndex,
    std::uint64_t result) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto appendNumber = [&hash](std::uint64_t value) noexcept {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            hash = (hash ^ static_cast<unsigned char>(value >> shift)) * 1099511628211ULL;
        }
    };
    if (action != nullptr) {
        for (const unsigned char character : std::string_view{action}) {
            hash = (hash ^ character) * 1099511628211ULL;
        }
    }
    appendNumber(requestedLobby);
    appendNumber(current.id);
    appendNumber(current.ownerPlatformId);
    appendNumber(current.memberCount);
    appendNumber(current.memberCount > 0 ? current.members[0].platformId : 0);
    appendNumber(current.memberCount > 1 ? current.members[1].platformId : 0);
    appendNumber(static_cast<std::uint64_t>(static_cast<std::int64_t>(memberIndex)));
    appendNumber(result);
    return hash == 0 ? 1 : hash;
}

[[nodiscard]] bool remember_current_lobby_observation(std::uint64_t hash) noexcept {
    for (std::atomic<std::uint64_t>& entry : g_currentLobbyObservations) {
        std::uint64_t observed = entry.load(std::memory_order_acquire);
        if (observed == hash) {
            return false;
        }
        if (observed == 0
            && entry.compare_exchange_strong(
                observed, hash, std::memory_order_acq_rel, std::memory_order_acquire)) {
            return true;
        }
    }
    return false;
}

void report_current_lobby_query(const char* action,
                                std::uint64_t requestedLobby,
                                const wire::Lobby& current,
                                int memberIndex,
                                std::uint64_t result) noexcept {
    if (!remember_current_lobby_observation(current_lobby_observation_hash(
            action, requestedLobby, current, memberIndex, result))) {
        return;
    }
    const std::uint64_t member0 =
        current.memberCount > 0 ? current.members[0].platformId : 0;
    const std::uint64_t member1 =
        current.memberCount > 1 ? current.members[1].platformId : 0;
    std::array<char, 512> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=steam_lobby_read stage=current query=%s requested=0x%016llX current=0x%016llX "
        "index=%d result=0x%016llX local=0x%016llX owner=0x%016llX members=%llu "
        "member0=0x%016llX member1=0x%016llX mutation=observe_only",
        action != nullptr ? action : "unknown",
        static_cast<unsigned long long>(requestedLobby),
        static_cast<unsigned long long>(current.id),
        memberIndex,
        static_cast<unsigned long long>(result),
        static_cast<unsigned long long>(local_steam_id()),
        static_cast<unsigned long long>(current.ownerPlatformId),
        static_cast<unsigned long long>(current.memberCount),
        static_cast<unsigned long long>(member0),
        static_cast<unsigned long long>(member1));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
}

} // namespace

ApiCall create_lobby([[maybe_unused]] void* self, int lobbyType, int maxMembers) noexcept {
    const ApiCall call = next_api_call();
    wire::Request request{};
    request.operation = wire::Operation::createLobby;
    request.valueA = lobbyType;
    // the archived client asks for its 32-slot managed-session ceiling, but an orbit fireteam
    // admits six, and the shared lobby protocol exposes six slots
    request.valueB = (std::clamp)(maxMembers, 1, static_cast<int>(wire::kMemberCapacity));
    wire::Response response{};
    const bool success = execute_ok(request, response) && response.lobby.id != 0;
    const LobbyCreated created{success ? kResultOk : (response.result != 0 ? response.result
                                                                          : kResultFail),
                               0,
                               success ? response.lobby.id : 0};
    if (!queue_callback(kLobbyCreatedCallback, call, &created, sizeof(created))) {
        return 0;
    }
    if (success) {
        const LobbyEnter entered{response.lobby.id, 0, false, {}, kLobbyEnterSuccess};
        (void)queue_callback(kLobbyEnterCallback, 0, &entered, sizeof(entered));
        (void)queue_data_update(response.lobby.id, response.lobby.id);
    }
    report("create", response.lobby.id, local_steam_id(), success);
    return call;
}

ApiCall join_lobby([[maybe_unused]] void* self, std::uint64_t lobby) noexcept {
    if (lobby == 0) {
        return 0;
    }
    const ApiCall call = next_api_call();
    if (!matchmaking_policy::central_lobby_id(lobby)) {
        // the descriptor already authenticated and routed this direct session. Forwarding its
        // opaque id to the central social store makes it reject the lobby and tear down TARGET
        const LobbyEnter entered{lobby, 0, false, {}, kLobbyEnterSuccess};
        const bool queued = queue_callback(kLobbyEnterCallback, call, &entered, sizeof(entered));
        report("join_managed_session", lobby, local_steam_id(), queued);
        return queued ? call : 0;
    }
    wire::Request request{};
    request.operation = wire::Operation::joinLobby;
    request.lobbyId = lobby;
    wire::Response response{};
    const bool success = execute_ok(request, response) && response.lobby.id == lobby;
    const LobbyEnter entered{lobby,
                             0,
                             false,
                             {},
                             success ? kLobbyEnterSuccess : kLobbyEnterFailure};
    if (!queue_callback(kLobbyEnterCallback, call, &entered, sizeof(entered))) {
        return 0;
    }
    if (success) {
        (void)queue_data_update(lobby, lobby);
    }
    report("join", lobby, response.lobby.ownerPlatformId, success);
    return call;
}

void leave_lobby([[maybe_unused]] void* self, std::uint64_t lobby) noexcept {
    if (lobby != 0 && !matchmaking_policy::central_lobby_id(lobby)) {
        report("leave_managed_session", lobby, local_steam_id(), true);
        return;
    }
    if (realtime::consume_internal_owner_leave_suppression(lobby)) {
        // an owner session rebase swaps the native GroupSession but keeps the central lobby;
        // forwarding the old handle's leave would expel the owner. Scoped to this transaction
        report("leave_suppressed_internal_owner_transition",
               lobby,
               local_steam_id(),
               true);
        return;
    }
    wire::Request request{};
    request.operation = wire::Operation::leaveLobby;
    request.lobbyId = lobby;
    wire::Response response{};
    const bool success = execute_ok(request, response);
    report("leave", lobby, local_steam_id(), success);
}

bool invite_user_to_lobby([[maybe_unused]] void* self,
                          std::uint64_t lobby,
                          std::uint64_t target) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::invite;
    request.lobbyId = lobby;
    request.targetPlatformId = target;
    wire::Response response{};
    const bool success = execute_ok(request, response);
    report("invite", lobby, target, success);
    return success;
}

int get_num_lobby_members([[maybe_unused]] void* self, std::uint64_t lobby) noexcept {
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    if (current.id == lobby) {
        const int count = static_cast<int>(current.memberCount);
        report_current_lobby_query(
            "member_count", lobby, current, -1, static_cast<std::uint64_t>(count));
        return count;
    }
    state::social::RemoteAccount remote{};
    const int count = remote_lobby(lobby, remote) ? 1 : 0;
    if (count != 0) {
        report_remote_query("member_count", lobby, count);
    }
    return count;
}

SteamId* get_lobby_member_by_index([[maybe_unused]] void* self,
                                   SteamId* result,
                                   std::uint64_t lobby,
                                   int memberIndex) noexcept {
    if (result == nullptr) {
        return nullptr;
    }
    result->value = 0;
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    if (current.id == lobby && memberIndex >= 0
        && static_cast<std::size_t>(memberIndex) < current.memberCount) {
        result->value = current.members[static_cast<std::size_t>(memberIndex)].platformId;
    } else if (memberIndex == 0) {
        state::social::RemoteAccount remote{};
        if (remote_lobby(lobby, remote)) {
            result->value = remote.platformId;
        }
    }
    if (current.id == lobby) {
        report_current_lobby_query(
            "member_by_index", lobby, current, memberIndex, result->value);
    }
    return result;
}

const char* get_lobby_data([[maybe_unused]] void* self,
                           std::uint64_t lobby,
                           const char* key) noexcept {
    thread_local std::array<char, wire::kValueCapacity> value{};
    value = {};
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    const wire::Metadata* const found = current.id == lobby ? metadata(current, key) : nullptr;
    if (found != nullptr) {
        value = found->value;
    } else {
        state::social::RemoteAccount remote{};
        if (remote_lobby(lobby, remote)) {
            report_remote_query("data", lobby, 0, key);
        }
    }
    return value.data();
}

bool set_lobby_data([[maybe_unused]] void* self,
                    std::uint64_t lobby,
                    const char* key,
                    const char* value) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::setLobbyData;
    request.lobbyId = lobby;
    if (!assign_text(request.key, key) || value == nullptr
        || std::strlen(value) >= request.value.size()) {
        return false;
    }
    std::memcpy(request.value.data(), value, std::strlen(value) + 1U);
    wire::Response response{};
    const bool success = execute_ok(request, response);
    return success && queue_data_update(lobby, lobby);
}

int get_lobby_data_count([[maybe_unused]] void* self, std::uint64_t lobby) noexcept {
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    return current.id == lobby ? static_cast<int>(current.metadataCount) : 0;
}

bool get_lobby_data_by_index([[maybe_unused]] void* self,
                             std::uint64_t lobby,
                             int metadataIndex,
                             char* key,
                             int keyCapacity,
                             char* value,
                             int valueCapacity) noexcept {
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    if (current.id != lobby || metadataIndex < 0
        || static_cast<std::size_t>(metadataIndex) >= current.metadataCount) {
        return false;
    }
    const wire::Metadata& entry = current.metadata[static_cast<std::size_t>(metadataIndex)];
    return copy_text(entry.key.data(), key, keyCapacity)
           && copy_text(entry.value.data(), value, valueCapacity);
}

bool delete_lobby_data([[maybe_unused]] void* self,
                       std::uint64_t lobby,
                       const char* key) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::deleteLobbyData;
    request.lobbyId = lobby;
    if (!assign_text(request.key, key)) {
        return false;
    }
    wire::Response response{};
    const bool success = execute_ok(request, response);
    return success && queue_data_update(lobby, lobby);
}

const char* get_lobby_member_data([[maybe_unused]] void* self,
                                  std::uint64_t lobby,
                                  std::uint64_t memberId,
                                  const char* key) noexcept {
    thread_local std::array<char, wire::kValueCapacity> value{};
    value = {};
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    const wire::Metadata* const found =
        current.id == lobby ? member_metadata(current, memberId, key) : nullptr;
    if (found != nullptr) {
        value = found->value;
    }
    return value.data();
}

void set_lobby_member_data([[maybe_unused]] void* self,
                           std::uint64_t lobby,
                           const char* key,
                           const char* value) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::setMemberData;
    request.lobbyId = lobby;
    if (!assign_text(request.key, key) || value == nullptr
        || std::strlen(value) >= request.value.size()) {
        return;
    }
    std::memcpy(request.value.data(), value, std::strlen(value) + 1U);
    wire::Response response{};
    if (execute_ok(request, response)) {
        (void)queue_data_update(lobby, local_steam_id());
    }
}

bool send_lobby_chat([[maybe_unused]] void* self,
                     std::uint64_t lobby,
                     const void* data,
                     int size) noexcept {
    if (data == nullptr || size <= 0
        || static_cast<std::size_t>(size) > wire::kEventPayloadCapacity) {
        return false;
    }
    wire::Request request{};
    request.operation = wire::Operation::sendChat;
    request.lobbyId = lobby;
    request.payloadSize = static_cast<std::size_t>(size);
    std::memcpy(request.payload.data(), data, request.payloadSize);
    wire::Response response{};
    return execute_ok(request, response);
}

int get_lobby_chat_entry([[maybe_unused]] void* self,
                         std::uint64_t lobby,
                         int messageIndex,
                         std::uint64_t* sender,
                         void* data,
                         int dataCapacity,
                         int* entryType) noexcept {
    return realtime::copy_chat_entry(
        lobby, messageIndex, sender, data, dataCapacity, entryType);
}

bool request_lobby_data([[maybe_unused]] void* self, std::uint64_t lobby) noexcept {
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    if (current.id == lobby) {
        return queue_data_update(lobby, lobby);
    }
    state::social::RemoteAccount remote{};
    const bool success = remote_lobby(lobby, remote)
                         && queue_data_update(lobby, remote.platformId);
    if (remote.platformId != 0) {
        report_remote_query("request_data", lobby, success ? 1 : 0);
    }
    return success;
}

void set_lobby_game_server([[maybe_unused]] void* self,
                           std::uint64_t lobby,
                           std::uint32_t ip,
                           std::uint16_t port,
                           std::uint64_t serverId) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::setGameServer;
    request.lobbyId = lobby;
    request.targetPlatformId = serverId;
    request.valueA = static_cast<std::int32_t>(ip);
    request.valueB = port;
    wire::Response response{};
    (void)execute_ok(request, response);
}

bool get_lobby_game_server([[maybe_unused]] void* self,
                           std::uint64_t lobby,
                           std::uint32_t* ip,
                           std::uint16_t* port,
                           SteamId* serverId) noexcept {
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    if (current.id != lobby
        || (current.gameServerPlatformId == 0 && current.gameServerIp == 0
            && current.gameServerPort == 0)) {
        return false;
    }
    if (ip != nullptr) {
        *ip = current.gameServerIp;
    }
    if (port != nullptr) {
        *port = current.gameServerPort;
    }
    if (serverId != nullptr) {
        serverId->value = current.gameServerPlatformId;
    }
    return true;
}

bool set_lobby_member_limit([[maybe_unused]] void* self,
                            std::uint64_t lobby,
                            int maximumMembers) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::setMemberLimit;
    request.lobbyId = lobby;
    request.valueA = maximumMembers;
    wire::Response response{};
    return execute_ok(request, response);
}

int get_lobby_member_limit([[maybe_unused]] void* self, std::uint64_t lobby) noexcept {
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    if (current.id == lobby) {
        return current.maximumMembers;
    }
    state::social::RemoteAccount remote{};
    const int limit = remote_lobby(lobby, remote)
                          ? static_cast<int>(wire::kMemberCapacity)
                          : 0;
    if (limit != 0) {
        report_remote_query("member_limit", lobby, limit);
    }
    return limit;
}

bool set_lobby_type([[maybe_unused]] void* self, std::uint64_t lobby, int type) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::setLobbyType;
    request.lobbyId = lobby;
    request.valueA = type;
    wire::Response response{};
    return execute_ok(request, response);
}

bool set_lobby_joinable([[maybe_unused]] void* self,
                        std::uint64_t lobby,
                        bool joinable) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::setJoinable;
    request.lobbyId = lobby;
    request.valueA = joinable ? 1 : 0;
    wire::Response response{};
    return execute_ok(request, response);
}

SteamId* get_lobby_owner([[maybe_unused]] void* self,
                         SteamId* result,
                         std::uint64_t lobby) noexcept {
    if (result == nullptr) {
        return nullptr;
    }
    const wire::Lobby current = realtime::lobby_snapshot(lobby, true);
    if (current.id == lobby) {
        result->value = current.ownerPlatformId;
        report_current_lobby_query("owner", lobby, current, -1, result->value);
    } else {
        state::social::RemoteAccount remote{};
        result->value = remote_lobby(lobby, remote) ? remote.platformId : 0;
        if (result->value != 0) {
            report_remote_query("owner", lobby, static_cast<long long>(result->value));
        }
    }
    return result;
}

bool set_lobby_owner([[maybe_unused]] void* self,
                     std::uint64_t lobby,
                     std::uint64_t newOwner) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::setOwner;
    request.lobbyId = lobby;
    request.targetPlatformId = newOwner;
    wire::Response response{};
    return execute_ok(request, response);
}

} // namespace dawn::steam::interfaces::methods
