#include "../../../core/settings/settings.h"
#include "../../../core/logging/log.h"
#include "../../../state/social/social_state.h"
#include "../../runtime/social/realtime_client.h"
#include "../internal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string_view>

namespace dawn::steam::interfaces::methods {
namespace {

constexpr int kPersonaStateOffline = 0;
constexpr int kPersonaStateOnline = 1;
constexpr int kFriendRelationshipNone = 0;
constexpr int kFriendFlagFriendshipRequested = 0x02;
constexpr int kFriendFlagImmediate = 0x04;
constexpr int kFriendFlagRequestingFriendship = 0x80;
constexpr int kFriendFlagBlocked = 0x01;
constexpr int kFriendFlagIgnored = 0x200;
constexpr int kFriendFlagIgnoredFriend = 0x400;
constexpr int kFriendFlagAll = 0xFFFF;
constexpr int kPersonaStateChangeCallback = 304;
constexpr int kFriendRichPresenceUpdateCallback = 336;
constexpr std::int32_t kPersonaChangeNameStateOnline = 0x01 | 0x02 | 0x04;
constexpr std::int32_t kRemotePersonaChange =
    kPersonaChangeNameStateOnline | 0x10 | 0x0200 | 0x0400;
constexpr char kEmpty[] = "";
constexpr char kUnknown[] = "[unknown]";
// the 64-bit PersonaStateChange_t ABI includes four bytes of tail alignment
struct PersonaStateChange {
    std::uint64_t steamId{};
    std::int32_t changeFlags{};
    std::int32_t padding{};
};

static_assert(sizeof(PersonaStateChange) == 16);

#pragma pack(push, 1)
struct FriendRichPresenceUpdate {
    std::uint64_t steamId{};
    std::uint32_t appId{};
};
#pragma pack(pop)

static_assert(sizeof(FriendRichPresenceUpdate) == 12);

struct FriendGameInfo {
    std::uint64_t gameId{};
    std::uint32_t gameIp{};
    std::uint16_t gamePort{};
    std::uint16_t queryPort{};
    std::uint64_t lobbySteamId{};
};

static_assert(sizeof(FriendGameInfo) == 24);

[[nodiscard]] bool is_local(std::uint64_t steamId) noexcept {
    return steamId == local_steam_id();
}

[[nodiscard]] bool remote_account(std::uint64_t steamId,
                                  state::social::RemoteAccount& output) noexcept {
    const state::social::Snapshot social = state::social::snapshot();
    for (std::size_t index = 0; index < social.remoteCount; ++index) {
        if (social.remotes[index].platformId == steamId) {
            output = social.remotes[index];
            return true;
        }
    }
    output = {};
    return false;
}

[[nodiscard]] int relationship_flag(state::social::Relationship relationship) noexcept {
    switch (relationship) {
    case state::social::Relationship::blocked: return kFriendFlagBlocked;
    case state::social::Relationship::requestRecipient: return kFriendFlagFriendshipRequested;
    case state::social::Relationship::friendAccount: return kFriendFlagImmediate;
    case state::social::Relationship::requestInitiator: return kFriendFlagRequestingFriendship;
    case state::social::Relationship::ignored: return kFriendFlagIgnored;
    case state::social::Relationship::ignoredFriend: return kFriendFlagIgnoredFriend;
    default: return 0;
    }
}

[[nodiscard]] bool included(const state::social::RemoteAccount& remote, int flags) noexcept {
    return flags == kFriendFlagAll || (flags & relationship_flag(remote.relationship)) != 0;
}

[[nodiscard]] std::size_t filtered_count(const state::social::Snapshot& social,
                                         int flags) noexcept {
    std::size_t count = 0;
    for (std::size_t index = 0; index < social.remoteCount; ++index) {
        count += included(social.remotes[index], flags) ? 1U : 0U;
    }
    return count;
}

template <std::size_t Capacity>
[[nodiscard]] bool assign_text(std::array<char, Capacity>& destination,
                               const char* source,
                               bool allowEmpty) noexcept {
    destination = {};
    if (source == nullptr) {
        return allowEmpty;
    }
    const std::size_t length = std::strlen(source);
    if (length >= destination.size() || (!allowEmpty && length == 0)) {
        return false;
    }
    std::copy_n(source, length, destination.begin());
    return true;
}

} // namespace

const char* persona_name([[maybe_unused]] void* self) noexcept {
    const state::social::Snapshot social = state::social::snapshot();
    if (social.localDisplayName.front() != '\0') {
        thread_local std::array<char, state::social::kNameCapacity> name{};
        name = social.localDisplayName;
        return name.data();
    }
    const char* const name = core::settings::get().steam.user.personaName.data();
    return name;
}

int persona_state([[maybe_unused]] void* self) noexcept {
    return kPersonaStateOnline;
}

int get_friend_count([[maybe_unused]] void* self,
                     int friendFlags) noexcept {
    const state::social::Snapshot social = state::social::snapshot();
    const int count = static_cast<int>(filtered_count(social, friendFlags));
    static std::atomic<int> lastFlags{-1};
    static std::atomic<int> lastCount{-1};
    if (lastFlags.exchange(friendFlags, std::memory_order_relaxed) != friendFlags
        || lastCount.exchange(count, std::memory_order_relaxed) != count) {
        std::array<char, 128> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=steam_friends stage=count flags=0x%X result=%d",
                                          static_cast<unsigned>(friendFlags),
                                          count);
        if (written > 0) {
            core::log::write(core::log::Channel::client,
                             core::log::Level::info,
                             {line.data(), static_cast<std::size_t>(written)});
        }
    }
    return count;
}

SteamId* get_friend_by_index([[maybe_unused]] void* self,
                             SteamId* result,
                             int friendIndex,
                             int friendFlags) noexcept {
    if (result == nullptr) {
        return nullptr;
    }
    result->value = 0;
    if (friendIndex < 0) {
        return result;
    }
    const state::social::Snapshot social = state::social::snapshot();
    int selected = 0;
    for (std::size_t index = 0; index < social.remoteCount; ++index) {
        if (!included(social.remotes[index], friendFlags)) {
            continue;
        }
        if (selected++ == friendIndex) {
            result->value = social.remotes[index].platformId;
            break;
        }
    }
    return result;
}

int friend_relationship([[maybe_unused]] void* self,
                        std::uint64_t friendSteamId) noexcept {
    state::social::RemoteAccount remote{};
    return remote_account(friendSteamId, remote)
               ? static_cast<int>(remote.relationship)
               : kFriendRelationshipNone;
}

int friend_persona_state([[maybe_unused]] void* self, std::uint64_t friendSteamId) noexcept {
    state::social::RemoteAccount remote{};
    return is_local(friendSteamId)
               ? kPersonaStateOnline
               : (remote_account(friendSteamId, remote) ? remote.personaState
                                                        : kPersonaStateOffline);
}

// Steam guarantees this never returns null; unknown remote ids use its documented
// unknown token
const char* friend_persona_name([[maybe_unused]] void* self,
                                std::uint64_t friendSteamId) noexcept {
    if (is_local(friendSteamId)) {
        const char* const name = persona_name(nullptr);
        return name;
    }
    state::social::RemoteAccount remote{};
    if (!remote_account(friendSteamId, remote)) {
        return kUnknown;
    }
    thread_local std::array<char, state::social::kNameCapacity> name{};
    name = remote.displayName;
    return name.data();
}

bool friend_game_played([[maybe_unused]] void* self,
                        std::uint64_t friendSteamId,
                        void* gameInfo) noexcept {
    state::social::RemoteAccount remote{};
    const bool known = remote_account(friendSteamId, remote);
    const bool available = gameInfo != nullptr && known
                           && remote.relationship == state::social::Relationship::friendAccount
                           && remote.appId != 0;
    if (!available) {
        return false;
    }
    FriendGameInfo info{};
    info.gameId = remote.appId;
    info.lobbySteamId = remote.lobbyId;
    std::memcpy(gameInfo, &info, sizeof info);
    return true;
}

const char* friend_persona_name_history([[maybe_unused]] void* self,
                                        std::uint64_t friendSteamId,
                                        int nameIndex) noexcept {
    const char* const result =
        nameIndex == 0 ? friend_persona_name(nullptr, friendSteamId) : kEmpty;
    return result;
}

int friend_steam_level([[maybe_unused]] void* self,
                       [[maybe_unused]] std::uint64_t friendSteamId) noexcept {
    return 0;
}

const char* player_nickname([[maybe_unused]] void* self,
                            [[maybe_unused]] std::uint64_t playerSteamId) noexcept {
    return nullptr;
}

bool has_friend([[maybe_unused]] void* self,
                std::uint64_t friendSteamId,
                int friendFlags) noexcept {
    state::social::RemoteAccount remote{};
    return remote_account(friendSteamId, remote) && included(remote, friendFlags);
}

int friend_avatar([[maybe_unused]] void* self,
                  [[maybe_unused]] std::uint64_t friendSteamId) noexcept {
    return 0;
}

bool request_user_information([[maybe_unused]] void* self,
                               std::uint64_t userSteamId,
                               [[maybe_unused]] bool requireNameOnly) noexcept {
    state::social::RemoteAccount remote{};
    const bool local = is_local(userSteamId);
    const bool knownRemote = !local && remote_account(userSteamId, remote);
    if (local || knownRemote) {
        const PersonaStateChange change{
            userSteamId,
            local ? kPersonaChangeNameStateOnline : kRemotePersonaChange,
            0};
        (void)queue_callback(kPersonaStateChangeCallback, 0, &change, sizeof(change));
    }
    return true;
}

DWORD user_restrictions([[maybe_unused]] void* self) noexcept {
    return 0;
}

bool set_rich_presence([[maybe_unused]] void* self,
                       const char* key,
                       const char* value) noexcept {
    namespace realtime = runtime::social::realtime_client;
    return realtime::stage_rich_presence(key, value);
}

void clear_rich_presence([[maybe_unused]] void* self) noexcept {
    namespace realtime = runtime::social::realtime_client;
    realtime::stage_clear_rich_presence();
}

const char* friend_rich_presence([[maybe_unused]] void* self,
                                 std::uint64_t friendSteamId,
                                 const char* key) noexcept {
    state::social::RemoteAccount remote{};
    if (key == nullptr || !remote_account(friendSteamId, remote)) {
        return kEmpty;
    }
    const std::string_view requested(key);
    if (requested == "status") {
        thread_local std::array<char, state::social::kLocationCapacity> location{};
        location = remote.location;
        return location.data();
    }
    if (requested == "steam_display") {
        thread_local std::array<char, state::social::kSteamDisplayCapacity> value{};
        value = remote.steamDisplay;
        return value.data();
    }
    if (requested == "connect") {
        thread_local std::array<char, state::social::kConnectCapacity> value{};
        value = remote.connect;
        return value.data();
    }
    if (requested == "steam_player_group") {
        thread_local std::array<char, state::social::kPlayerGroupCapacity> value{};
        value = remote.playerGroup;
        return value.data();
    }
    if (requested == "steam_player_group_size") {
        thread_local std::array<char, state::social::kPlayerGroupSizeCapacity> value{};
        value = remote.playerGroupSize;
        return value.data();
    }
    return kEmpty;
}

int friend_rich_presence_key_count([[maybe_unused]] void* self,
                                   std::uint64_t friendSteamId) noexcept {
    state::social::RemoteAccount remote{};
    if (!remote_account(friendSteamId, remote)) {
        return 0;
    }
    const int count = (remote.location.front() != '\0' ? 1 : 0)
                      + (remote.steamDisplay.front() != '\0' ? 1 : 0)
                      + (remote.connect.front() != '\0' ? 1 : 0)
                      + (remote.playerGroup.front() != '\0' ? 1 : 0)
                      + (remote.playerGroupSize.front() != '\0' ? 1 : 0);
    return count;
}

const char* friend_rich_presence_key_by_index(
    [[maybe_unused]] void* self,
    std::uint64_t friendSteamId,
    int keyIndex) noexcept {
    state::social::RemoteAccount remote{};
    if (!remote_account(friendSteamId, remote) || keyIndex < 0) {
        return kEmpty;
    }
    static constexpr std::array<const char*, 5> kKeys{
        "status", "steam_display", "connect",
        "steam_player_group", "steam_player_group_size"};
    const std::array<bool, 5> present{
        remote.location.front() != '\0',
        remote.steamDisplay.front() != '\0',
        remote.connect.front() != '\0',
        remote.playerGroup.front() != '\0',
        remote.playerGroupSize.front() != '\0'};
    int visibleIndex = 0;
    for (std::size_t index = 0; index < kKeys.size(); ++index) {
        if (present[index] && visibleIndex++ == keyIndex) {
            return kKeys[index];
        }
    }
    return kEmpty;
}

// the client commits its native local user, requests its presence and expects callback 336
// on a later RunCallbacks batch, so queue the completion here to keep that ordering
void request_friend_rich_presence([[maybe_unused]] void* self,
                                  std::uint64_t friendSteamId) noexcept {
    if (friendSteamId == 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=steam_friend_presence_request callback=336 result=invalid_id");
        return;
    }
    const FriendRichPresenceUpdate update{friendSteamId, app_id()};
    const bool queued = queue_callback(
        kFriendRichPresenceUpdateCallback, 0, &update, sizeof(update));
    core::log::write(core::log::Channel::client,
                     queued ? core::log::Level::info : core::log::Level::error,
                     queued
                         ? "ev=steam_friend_presence_request callback=336 result=queued"
                         : "ev=steam_friend_presence_request callback=336 result=queue_failed");
}

bool invite_user_to_game([[maybe_unused]] void* self,
                         std::uint64_t friendSteamId,
                         const char* connectString) noexcept {
    state::social::RemoteAccount remote{};
    if (connectString == nullptr || *connectString == '\0'
        || !remote_account(friendSteamId, remote)
        || remote.relationship != state::social::Relationship::friendAccount) {
        return false;
    }
    namespace realtime = runtime::social::realtime_client;
    namespace wire = middleware::social::realtime;
    wire::Lobby lobby = realtime::lobby_snapshot();
    wire::Response response{};
    if (lobby.id == 0) {
        wire::Request create{};
        create.operation = wire::Operation::createLobby;
        create.valueA = 1;
        create.valueB = static_cast<std::int32_t>(wire::kMemberCapacity);
        if (!realtime::execute(create, response) || response.result != 1
            || response.lobby.id == 0) {
            return false;
        }
        lobby = response.lobby;
    }
    wire::Request invite{};
    invite.operation = wire::Operation::invite;
    invite.lobbyId = lobby.id;
    invite.targetPlatformId = friendSteamId;
    invite.payloadSize = (std::min)(std::strlen(connectString), invite.payload.size());
    std::memcpy(invite.payload.data(), connectString, invite.payloadSize);
    const bool success = realtime::execute(invite, response) && response.result == 1;
    std::array<char, 224> line{};
    const int written = std::snprintf(
        line.data(),
        line.size(),
        "ev=steam stage=friend_invite action=invite_user_to_game result=%s"
        " lobby=0x%016llX target=0x%016llX payload_bytes=%zu",
        success ? "ok" : "fail",
        static_cast<unsigned long long>(lobby.id),
        static_cast<unsigned long long>(friendSteamId),
        invite.payloadSize);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         success ? core::log::Level::info : core::log::Level::warn,
                         {line.data(),
                          (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
    }
    return success;
}

bool set_listen_for_friends_messages([[maybe_unused]] void* self,
                                     [[maybe_unused]] bool interceptEnabled) noexcept {
    return true;
}

bool reply_to_friend_message([[maybe_unused]] void* self,
                             [[maybe_unused]] std::uint64_t friendSteamId,
                             [[maybe_unused]] const char* message) noexcept {
    return false;
}

} // namespace dawn::steam::interfaces::methods
