#include "realtime_store.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

#include "core/logging/log.h"
#include "middleware/social/membership_projection.h"
#include "middleware/social/rendezvous_batch.h"
#include "state/activity/shared/public_admission.h"
#include "state/matchmaking/social_search.h"

namespace restoration::realtime_store {
namespace {

namespace wire = dawn::middleware::social::realtime;

// lobby ids are CSteamID chat accounts with the lobby flag: universe 1<<56 | type 8<<52 |
// instance 0x40000<<32. The old 0x0109 prefix decoded as type 0 and broke fireteam preflight
constexpr std::uint64_t kLobbyIdPrefix = 0x0184000000000000ULL;
constexpr std::uint64_t kLobbyAccountIdMask = 0x00000000FFFFFFFFULL;
constexpr std::uint64_t kPresenceLifetimeSeconds = 30;
constexpr std::size_t kPresenceCapacity = 128;
constexpr std::size_t kEventRecordCapacity = 256;
constexpr std::size_t kInviteCapacity = 128;
constexpr std::uint64_t kInviteLifetimeSeconds = 120;
constexpr std::int32_t kResultOk = 1;
constexpr std::int32_t kResultFail = 2;
constexpr std::int32_t kResultInvalidParameter = 8;
constexpr std::int32_t kResultAccessDenied = 15;
constexpr std::int32_t kResultLimitExceeded = 25;
constexpr std::string_view kConnectPrefix = "/connect:";
constexpr std::size_t kConnectIdentityBytes = 36;
constexpr std::size_t kConnectTokenSize =
    kConnectPrefix.size() + kConnectIdentityBytes * 2U;
constexpr std::string_view kOwnerAdmissionDiagnosticPollKey =
    "__dawn_owner_admission_diag_v1";
constexpr std::size_t kDiagnosticLineCapacity = 768;

struct LobbyRecord final {
    wire::Lobby lobby{};
    GameplayBootstrapDescriptor nativeFireteamDescriptor{};
    std::uint64_t descriptorOwnerPlatformId{};
    bool nativeFireteamDescriptorReady{};
    bool bootstrapUnclaimed{};
    bool occupied{};
};

struct PresenceRecord final {
    std::uint64_t platformId{};
    std::uint64_t accountSoid{};
    std::uint64_t sessionGeneration{};
    std::uint64_t lobbyId{};
    std::uint64_t fireteamLobbyId{};
    std::uint64_t lastSeen{};
    bool occupied{};
    bool gameplayReady{};
    std::array<char, dawn::state::social::kLocationCapacity> status{};
    std::array<char, dawn::state::social::kSteamDisplayCapacity> steamDisplay{};
    std::array<char, dawn::state::social::kConnectCapacity> connect{};
    std::array<char, dawn::state::social::kPlayerGroupCapacity> playerGroup{};
    std::array<char, dawn::state::social::kPlayerGroupSizeCapacity> playerGroupSize{};
};

struct EventRecord final {
    std::uint64_t recipientPlatformId{};
    wire::Event event{};
    bool occupied{};
};

struct InviteRecord final {
    std::uint64_t lobbyId{};
    std::uint64_t sourcePlatformId{};
    std::uint64_t recipientPlatformId{};
    std::uint64_t expiresAt{};
    bool occupied{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
GameplayBootstrapDescriptorProvider g_gameplayBootstrapDescriptorProvider{};
std::array<LobbyRecord, wire::kLobbyCapacity> g_lobbies{};
std::array<PresenceRecord, kPresenceCapacity> g_presence{};
std::array<EventRecord, kEventRecordCapacity> g_events{};
std::array<InviteRecord, kInviteCapacity> g_invites{};
std::uint64_t g_nextLobby = 1;
std::uint64_t g_nextEvent = 1;

constexpr std::size_t kDescriptorMachineOffset = 0;
constexpr std::size_t kDescriptorMethodOffset = 93;
constexpr std::size_t kDescriptorSessionOffset = 110;
constexpr std::size_t kDescriptorNetAddrOffset = 8;
constexpr std::size_t kDescriptorNetAddrSize = 86;

void emit_diagnostic(DiagnosticSink sink,
                     DiagnosticLevel level,
                     const std::array<char, kDiagnosticLineCapacity>& line,
                     int written) noexcept {
    if (sink == nullptr || written <= 0) {
        return;
    }
    const std::size_t length =
        (std::min)(static_cast<std::size_t>(written), line.size() - 1U);
    sink(level, {line.data(), length});
}

[[nodiscard]] LobbyRecord* find_lobby(std::uint64_t id) noexcept {
    for (LobbyRecord& record : g_lobbies) {
        if (record.occupied && record.lobby.id == id) {
            return &record;
        }
    }
    return nullptr;
}

[[nodiscard]] PresenceRecord* find_presence(std::uint64_t platformId) noexcept {
    for (PresenceRecord& record : g_presence) {
        if (record.occupied && record.platformId == platformId) {
            return &record;
        }
    }
    return nullptr;
}

[[nodiscard]] bool presence_fresh(const PresenceRecord& presence,
                                  std::uint64_t nowSeconds) noexcept {
    return presence.occupied
           && (nowSeconds < presence.lastSeen
               || nowSeconds - presence.lastSeen <= kPresenceLifetimeSeconds);
}

template <typename Value>
[[nodiscard]] Value read_descriptor_value(
    const GameplayBootstrapDescriptor& descriptor,
    std::size_t offset) noexcept {
    Value value{};
    if (offset <= descriptor.size() && sizeof(value) <= descriptor.size() - offset) {
        std::memcpy(&value, descriptor.data() + offset, sizeof(value));
    }
    return value;
}

[[nodiscard]] bool valid_native_fireteam_descriptor(
    const GameplayBootstrapDescriptor& descriptor,
    std::uint64_t lobbyId) noexcept {
    const std::uint64_t machineId =
        read_descriptor_value<std::uint64_t>(descriptor, kDescriptorMachineOffset);
    const std::uint64_t onlineSessionId =
        read_descriptor_value<std::uint64_t>(descriptor, kDescriptorSessionOffset);
    const std::uint8_t method =
        std::to_integer<std::uint8_t>(descriptor[kDescriptorMethodOffset]);
    const bool netAddrPresent = std::any_of(
        descriptor.begin() + static_cast<std::ptrdiff_t>(kDescriptorNetAddrOffset),
        descriptor.begin() + static_cast<std::ptrdiff_t>(kDescriptorNetAddrOffset
                                                         + kDescriptorNetAddrSize),
        [](std::byte value) noexcept { return value != std::byte{}; });
    return lobbyId != 0 && machineId != 0 && onlineSessionId == lobbyId
           && method <= 7U && netAddrPresent;
}

[[nodiscard]] PresenceRecord* touch(std::uint64_t platformId,
                                    std::uint64_t accountSoid,
                                    std::uint64_t nowSeconds) noexcept {
    PresenceRecord* free = nullptr;
    for (PresenceRecord& record : g_presence) {
        if (record.occupied && record.platformId == platformId) {
            // SignOn binds one platform to one account per generation; heartbeats must not rewrite that
            if (record.sessionGeneration != 0 && record.accountSoid != accountSoid) {
                return nullptr;
            }
            if (record.sessionGeneration == 0) {
                record.accountSoid = accountSoid;
            }
            record.lastSeen = nowSeconds;
            return &record;
        }
        if (!record.occupied && free == nullptr) {
            free = &record;
        }
    }
    if (free != nullptr) {
        *free = {};
        free->platformId = platformId;
        free->accountSoid = accountSoid;
        free->lastSeen = nowSeconds;
        free->occupied = true;
    }
    return free;
}

template <std::size_t Capacity>
[[nodiscard]] bool assign_text(
    std::array<char, Capacity>& destination,
    const std::array<char, wire::kValueCapacity>& source) noexcept {
    const std::size_t length = std::strlen(source.data());
    if (length >= destination.size()) {
        return false;
    }
    destination = {};
    std::copy_n(source.begin(), length, destination.begin());
    return true;
}

[[nodiscard]] std::int32_t set_rich_presence(
    PresenceRecord& presence,
    const wire::Request& request) noexcept {
    const char* const key = request.key.data();
    if (std::strcmp(key, "status") == 0) {
        return assign_text(presence.status, request.value)
                   ? kResultOk : kResultInvalidParameter;
    }
    if (std::strcmp(key, "steam_display") == 0) {
        return assign_text(presence.steamDisplay, request.value)
                   ? kResultOk : kResultInvalidParameter;
    }
    if (std::strcmp(key, "connect") == 0) {
        return assign_text(presence.connect, request.value)
                   ? kResultOk : kResultInvalidParameter;
    }
    if (std::strcmp(key, "steam_player_group") == 0) {
        return assign_text(presence.playerGroup, request.value)
                   ? kResultOk : kResultInvalidParameter;
    }
    if (std::strcmp(key, "steam_player_group_size") == 0) {
        return assign_text(presence.playerGroupSize, request.value)
                   ? kResultOk : kResultInvalidParameter;
    }
    return kResultOk;
}

void clear_rich_presence(PresenceRecord& presence) noexcept {
    presence.status = {};
    presence.steamDisplay = {};
    presence.connect = {};
    presence.playerGroup = {};
    presence.playerGroupSize = {};
}

void discard_session_events(std::uint64_t platformId) noexcept {
    for (EventRecord& record : g_events) {
        if (!record.occupied) {
            continue;
        }
        const bool incoming = record.recipientPlatformId == platformId;
        const bool stalePayload = record.event.sourcePlatformId == platformId
                                  && (record.event.kind == wire::EventKind::lobbyInvite
                                      || record.event.kind == wire::EventKind::chatMessage
                                      || record.event.kind == wire::EventKind::rendezvous);
        if (incoming || stalePayload) {
            record = {};
        }
    }
}

void discard_session_invites(std::uint64_t platformId) noexcept {
    for (InviteRecord& invite : g_invites) {
        if (invite.occupied
            && (invite.sourcePlatformId == platformId
                || invite.recipientPlatformId == platformId)) {
            invite = {};
        }
    }
}

[[nodiscard]] wire::Member* find_member(wire::Lobby& lobby,
                                        std::uint64_t platformId) noexcept {
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        if (lobby.members[index].platformId == platformId) {
            return &lobby.members[index];
        }
    }
    return nullptr;
}

[[nodiscard]] const wire::Member* find_member(const wire::Lobby& lobby,
                                              std::uint64_t platformId) noexcept {
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        if (lobby.members[index].platformId == platformId) {
            return &lobby.members[index];
        }
    }
    return nullptr;
}

[[nodiscard]] bool retained_shared_member(const PresenceRecord& presence) noexcept {
    return std::any_of(g_lobbies.begin(),g_lobbies.end(),[&](const LobbyRecord& record) {
        return record.occupied && record.lobby.memberCount > 1U
            && find_member(record.lobby,presence.platformId) != nullptr;
    });
}

[[nodiscard]] std::uint64_t social_lobby_id(const PresenceRecord& presence) noexcept {
    const auto* bound=find_lobby(presence.fireteamLobbyId);
    return bound && find_member(bound->lobby,presence.platformId)
        ? bound->lobby.id : presence.lobbyId;
}

[[nodiscard]] bool shares_fireteam_locked(const PresenceRecord* left,
                                          const PresenceRecord* right) noexcept {
    if (left == nullptr || right == nullptr || left == right
        || !left->gameplayReady || !right->gameplayReady) return false;
    const auto lobbyId = social_lobby_id(*left);
    if (lobbyId == 0 || social_lobby_id(*right) != lobbyId) return false;
    const auto* lobby = find_lobby(lobbyId);
    const auto* owner = lobby ? find_presence(lobby->lobby.ownerPlatformId) : nullptr;
    return owner != nullptr && owner->gameplayReady
        && lobby->nativeFireteamDescriptorReady
        && social_lobby_id(*owner) == lobbyId
        && find_member(lobby->lobby, owner->platformId) != nullptr
        && find_member(lobby->lobby, left->platformId) != nullptr
        && find_member(lobby->lobby, right->platformId) != nullptr;
}

[[nodiscard]] std::size_t lobby_count(std::uint64_t platform,int type,std::uint64_t except=0) noexcept {
    return static_cast<std::size_t>(std::count_if(g_lobbies.begin(),g_lobbies.end(),[&](const LobbyRecord& record) {
        return record.occupied && record.lobby.id!=except && ((record.lobby.type==3)==(type==3))
            && find_member(record.lobby,platform)!=nullptr;
    }));
}

void select_remaining_lobby(PresenceRecord& presence) noexcept {
    if(presence.lobbyId!=0)return;
    for(const auto& record:g_lobbies) {
        if(record.occupied && find_member(record.lobby,presence.platformId)) {
            presence.lobbyId=record.lobby.id;
            if(record.lobby.id==presence.fireteamLobbyId)return;
        }
    }
}

[[nodiscard]] wire::Metadata* find_metadata(
    std::array<wire::Metadata, wire::kMetadataCapacity>& metadata,
    std::size_t count,
    const std::array<char, wire::kKeyCapacity>& key) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (std::strcmp(metadata[index].key.data(), key.data()) == 0) {
            return &metadata[index];
        }
    }
    return nullptr;
}

[[nodiscard]] bool decode_hex_nibble(char value, std::uint8_t& nibble) noexcept {
    if (value >= '0' && value <= '9') {
        nibble = static_cast<std::uint8_t>(value - '0');
        return true;
    }
    if (value >= 'a' && value <= 'f') {
        nibble = static_cast<std::uint8_t>(value - 'a' + 10);
        return true;
    }
    if (value >= 'A' && value <= 'F') {
        nibble = static_cast<std::uint8_t>(value - 'A' + 10);
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_connect_token(const PresenceRecord& presence,
                                       std::size_t& tokenSize) noexcept {
    const auto end = std::find(presence.connect.begin(), presence.connect.end(), '\0');
    tokenSize = static_cast<std::size_t>(end - presence.connect.begin());
    if (tokenSize != kConnectTokenSize
        || !std::equal(kConnectPrefix.begin(),
                       kConnectPrefix.end(),
                       presence.connect.begin())) {
        return false;
    }

    std::uint64_t encodedPlatformId = 0;
    for (std::size_t byteIndex = 0; byteIndex < kConnectIdentityBytes; ++byteIndex) {
        const std::size_t offset = kConnectPrefix.size() + byteIndex * 2U;
        std::uint8_t high = 0;
        std::uint8_t low = 0;
        if (!decode_hex_nibble(presence.connect[offset], high)
            || !decode_hex_nibble(presence.connect[offset + 1U], low)) {
            return false;
        }
        if (byteIndex < sizeof(encodedPlatformId)) {
            encodedPlatformId |= static_cast<std::uint64_t>((high << 4U) | low)
                                 << (byteIndex * 8U);
        }
    }
    return encodedPlatformId == presence.platformId;
}

[[nodiscard]] bool queue_event(std::uint64_t recipientPlatformId,
                               wire::Event event) noexcept {
    if (recipientPlatformId == 0 || event.kind < wire::EventKind::lobbyInvite
        || event.kind > wire::EventKind::rendezvousBatch
        || event.payloadSize > event.payload.size()) {
        return false;
    }
    event.sequence = g_nextEvent++;
    if (event.sequence == 0) {
        event.sequence = g_nextEvent++;
    }
    EventRecord* destination = nullptr;
    EventRecord* oldest = &g_events.front();
    for (EventRecord& record : g_events) {
        if (!record.occupied) {
            destination = &record;
            break;
        }
        if (!oldest->occupied || record.event.sequence < oldest->event.sequence) {
            oldest = &record;
        }
    }
    if (destination == nullptr) {
        destination = oldest;
    }
    *destination = {recipientPlatformId, event, true};
    return true;
}

void discard_gameplay_bootstrap_descriptor_events(std::uint64_t recipientPlatformId,
                                                  std::uint64_t lobbyId) noexcept {
    for (EventRecord& retained : g_events) {
        if (retained.occupied && retained.recipientPlatformId == recipientPlatformId
            && retained.event.kind == wire::EventKind::gameplayBootstrapDescriptor
            && retained.event.lobbyId == lobbyId) {
            retained = {};
        }
    }
}

[[nodiscard]] bool ensure_gameplay_bootstrap_descriptor(
    const LobbyRecord& lobbyRecord,
    std::uint64_t recipientPlatformId) noexcept {
    const wire::Lobby& lobby = lobbyRecord.lobby;
    if (lobby.id == 0 || lobby.ownerPlatformId == 0 || recipientPlatformId == 0
        || recipientPlatformId == lobby.ownerPlatformId
        || find_member(lobby, recipientPlatformId) == nullptr
        || !lobbyRecord.nativeFireteamDescriptorReady
        || lobbyRecord.descriptorOwnerPlatformId != lobby.ownerPlatformId) {
        return false;
    }
    for (const EventRecord& retained : g_events) {
        if (retained.occupied && retained.recipientPlatformId == recipientPlatformId
            && retained.event.kind == wire::EventKind::gameplayBootstrapDescriptor
            && retained.event.lobbyId == lobby.id
            && retained.event.sourcePlatformId == lobby.ownerPlatformId
            && retained.event.memberPlatformId == recipientPlatformId
            && retained.event.payloadSize
                   == dawn::middleware::gameplay::descriptor::kDescriptorSize) {
            return true;
        }
    }
    wire::Event event{};
    GameplayBootstrapDescriptor descriptor{};
    if (g_gameplayBootstrapDescriptorProvider == nullptr
        || !g_gameplayBootstrapDescriptorProvider(lobby.id, descriptor)) {
        // no provider is registered in the dedicated server, so joiners never got the owner's
        // descriptor and sat in Orbit. Read the owner's published one directly (we hold g_lock)
        if (!valid_native_fireteam_descriptor(lobbyRecord.nativeFireteamDescriptor, lobby.id)) {
            return false;
        }
        descriptor = lobbyRecord.nativeFireteamDescriptor;
    }
    std::array<char, kDiagnosticLineCapacity> sent{};
    const int sentLength = std::snprintf(
        sent.data(),
        sent.size(),
        "ev=realtime stage=bootstrap_descriptor result=queued recipient=0x%016llX lobby=0x%016llX "
        "owner=0x%016llX",
        static_cast<unsigned long long>(recipientPlatformId),
        static_cast<unsigned long long>(lobby.id),
        static_cast<unsigned long long>(lobby.ownerPlatformId));
    if (sentLength > 0) {
        dawn::core::log::write(dawn::core::log::Channel::server,
                                  dawn::core::log::Level::info,
                                  {sent.data(),
                                   (std::min)(static_cast<std::size_t>(sentLength),
                                              sent.size() - 1U)});
    }
    event.kind = wire::EventKind::gameplayBootstrapDescriptor;
    event.lobbyId = lobby.id;
    event.sourcePlatformId = lobby.ownerPlatformId;
    event.memberPlatformId = recipientPlatformId;
    event.payloadSize = descriptor.size();
    std::copy(descriptor.begin(), descriptor.end(), event.payload.begin());
    return queue_event(recipientPlatformId, event);
}

void publish_lobby_event(const wire::Lobby& lobby,
                          wire::EventKind kind,
                          std::uint64_t sourcePlatformId,
                          std::uint64_t memberPlatformId) noexcept {
    wire::Event event{};
    event.kind = kind;
    event.lobbyId = lobby.id;
    event.sourcePlatformId = sourcePlatformId;
    event.memberPlatformId = memberPlatformId;
    if(kind==wire::EventKind::memberChanged) {
        // callback 506 describes this lobby, not whichever one a client last queried
        const std::uint32_t change=find_member(lobby,memberPlatformId)?1U:2U;
        event.payloadSize=sizeof(change);
        std::memcpy(event.payload.data(),&change,sizeof(change));
    }
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        (void)queue_event(lobby.members[index].platformId, event);
    }
}

void destroy_lobby(LobbyRecord& record, std::uint64_t sourcePlatformId) noexcept {
    const wire::Lobby previous = record.lobby;
    wire::Event event{};
    event.kind = wire::EventKind::lobbyDestroyed;
    event.lobbyId = previous.id;
    event.sourcePlatformId = sourcePlatformId;
    for (std::size_t index = 0; index < previous.memberCount; ++index) {
        discard_gameplay_bootstrap_descriptor_events(previous.members[index].platformId,
                                                     previous.id);
        if (PresenceRecord* const memberPresence =
                find_presence(previous.members[index].platformId);
            memberPresence != nullptr) {
            if(memberPresence->lobbyId==previous.id)memberPresence->lobbyId = 0;
            if(memberPresence->fireteamLobbyId==previous.id)memberPresence->fireteamLobbyId = 0;
        }
        (void)queue_event(previous.members[index].platformId, event);
    }
    for (InviteRecord& invite : g_invites) {
        if (invite.occupied && invite.lobbyId == previous.id) {
            invite = {};
        }
    }
    record = {};
}

[[nodiscard]] bool leave_lobby(PresenceRecord& presence,
                               std::uint64_t sourcePlatformId,
                               std::uint64_t requestedLobby=0) noexcept {
    const auto lobbyId=requestedLobby!=0?requestedLobby:presence.lobbyId;
    LobbyRecord* const record = find_lobby(lobbyId);
    if (record == nullptr) {
        if(presence.lobbyId==lobbyId)presence.lobbyId=0;
        if(presence.fireteamLobbyId==lobbyId)presence.fireteamLobbyId=0;
        select_remaining_lobby(presence);
        return false;
    }
    wire::Lobby& lobby = record->lobby;
    std::size_t memberIndex = lobby.memberCount;
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        if (lobby.members[index].platformId == presence.platformId) {
            memberIndex = index;
            break;
        }
    }
    if (memberIndex == lobby.memberCount) {
        return false;
    }
    const std::uint64_t departed = lobby.members[memberIndex].platformId;
    discard_gameplay_bootstrap_descriptor_events(departed, lobby.id);
    for (std::size_t index = memberIndex + 1; index < lobby.memberCount; ++index) {
        lobby.members[index - 1] = lobby.members[index];
    }
    lobby.members[lobby.memberCount - 1] = {};
    --lobby.memberCount;
    if(presence.lobbyId==lobbyId)presence.lobbyId = 0;
    if(presence.fireteamLobbyId==lobbyId)presence.fireteamLobbyId = 0;
    select_remaining_lobby(presence);
    if (lobby.memberCount == 0) {
        destroy_lobby(*record, sourcePlatformId);
        return true;
    }
    if (lobby.ownerPlatformId == departed) {
        lobby.ownerPlatformId = lobby.members.front().platformId;
        for (std::size_t index = 0; index < lobby.memberCount; ++index) {
            discard_gameplay_bootstrap_descriptor_events(
                lobby.members[index].platformId, lobby.id);
        }
        record->nativeFireteamDescriptor = {};
        record->descriptorOwnerPlatformId = 0;
        record->nativeFireteamDescriptorReady = false;
    }
    ++lobby.revision;
    publish_lobby_event(lobby, wire::EventKind::memberChanged, sourcePlatformId, departed);
    return true;
}

[[nodiscard]] bool leave_replaced_lobby(PresenceRecord& presence,
                                       std::uint64_t lobbyId) noexcept {
    if (!leave_lobby(presence, presence.platformId, lobbyId)) return false;
    wire::Event departure{};
    departure.kind = wire::EventKind::memberChanged;
    departure.lobbyId = lobbyId;
    departure.sourcePlatformId = presence.platformId;
    departure.memberPlatformId = presence.platformId;
    constexpr std::uint32_t left = 2U;
    departure.payloadSize = sizeof(left);
    std::memcpy(departure.payload.data(), &left, sizeof(left));
    (void)queue_event(presence.platformId, departure);
    return true;
}

[[nodiscard]] std::uint64_t previous_native_fireteam(const PresenceRecord& presence,
                                                    const LobbyRecord& target) noexcept {
    const auto* bound = find_lobby(presence.fireteamLobbyId);
    return target.lobby.type == 3 && bound != nullptr && bound->lobby.type == 3
        && bound->lobby.id != target.lobby.id
        && find_member(bound->lobby, presence.platformId) != nullptr
        ? bound->lobby.id : 0U;
}

void leave_all_lobbies(PresenceRecord& presence) noexcept {
    for(auto& record:g_lobbies) {
        if(record.occupied && find_member(record.lobby,presence.platformId))
            (void)leave_lobby(presence,presence.platformId,record.lobby.id);
    }
}

void expire(std::uint64_t nowSeconds) noexcept {
    for (InviteRecord& invite : g_invites) {
        if (invite.occupied && nowSeconds >= invite.expiresAt) {
            invite = {};
        }
    }
    for (PresenceRecord& presence : g_presence) {
        if (!presence.occupied || presence_fresh(presence, nowSeconds)) {
            continue;
        }
        // polling only gives freshness. Treating one missed callback pump as LeaveLobby migrated
        // authority and the host sent a fatal one-member snapshot; keep membership until a real leave
        if (retained_shared_member(presence)) {
            continue;
        }
        leave_all_lobbies(presence);
        presence = {};
    }
}

[[nodiscard]] bool copy_events(std::uint64_t platformId,
                               std::uint64_t cursor,
                               wire::Response& response,
                               DiagnosticSink diagnosticSink) noexcept {
    std::array<const EventRecord*, kEventRecordCapacity> ordered{};
    std::size_t count = 0;
    for (const EventRecord& record : g_events) {
        if (record.occupied && record.recipientPlatformId == platformId
            && record.event.sequence > cursor) {
            ordered[count++] = &record;
        }
    }
    std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(count),
              [](const EventRecord* left, const EventRecord* right) noexcept {
                  return left->event.sequence < right->event.sequence;
              });
    response.cursor = cursor;
    response.eventCount = (std::min)(count, response.events.size());
    for (std::size_t index = 0; index < response.eventCount; ++index) {
        response.events[index] = ordered[index]->event;
        response.cursor = response.events[index].sequence;
        const wire::Event& event = response.events[index];
        if (event.kind == wire::EventKind::ownerGameplayAdmission) {
            std::array<char, kDiagnosticLineCapacity> line{};
            const int written = std::snprintf(
                line.data(),
                line.size(),
                "ev=owner_gameplay_admission stage=copy result=ok recipient=%llu lobby=%llu source=%llu owner=%llu sequence=%llu request_cursor=%llu response_cursor=%llu",
                static_cast<unsigned long long>(platformId),
                static_cast<unsigned long long>(event.lobbyId),
                static_cast<unsigned long long>(event.sourcePlatformId),
                static_cast<unsigned long long>(event.memberPlatformId),
                static_cast<unsigned long long>(event.sequence),
                static_cast<unsigned long long>(cursor),
                static_cast<unsigned long long>(response.cursor));
            emit_diagnostic(diagnosticSink, DiagnosticLevel::info, line, written);
        }
    }
    return true;
}

void copy_current_lobby(const PresenceRecord& presence, wire::Response& response) noexcept {
    if (const LobbyRecord* const record = find_lobby(social_lobby_id(presence)); record != nullptr) {
        response.lobby = record->lobby;
    }
}

[[nodiscard]] LobbyRecord* allocate_lobby() noexcept {
    for (LobbyRecord& record : g_lobbies) {
        if (!record.occupied) {
            return &record;
        }
    }
    return nullptr;
}

[[nodiscard]] bool accepted_friend(FriendshipCheck check,
                                   std::uint64_t left,
                                   std::uint64_t right) noexcept {
    return check != nullptr && left != 0 && right != 0 && left != right && check(left, right);
}

[[nodiscard]] std::uint64_t superseded_foreign_fireteam(const PresenceRecord& presence) noexcept {
    const auto* bound = find_lobby(presence.fireteamLobbyId);
    if (!presence.gameplayReady || lobby_count(presence.platformId,3) != 2U
        || bound == nullptr || bound->lobby.type != 3 || !bound->nativeFireteamDescriptorReady
        || bound->bootstrapUnclaimed || bound->lobby.ownerPlatformId == presence.platformId
        || bound->descriptorOwnerPlatformId != bound->lobby.ownerPlatformId
        || find_member(bound->lobby,presence.platformId) == nullptr
        || find_member(bound->lobby,bound->lobby.ownerPlatformId) == nullptr) return 0;
    const auto* owner = find_presence(bound->lobby.ownerPlatformId);
    if (owner == nullptr || !owner->gameplayReady
        || !presence_fresh(*owner,presence.lastSeen)) return 0;
    for (const auto& other : g_lobbies) {
        if (!other.occupied || other.lobby.type != 3 || other.lobby.id == bound->lobby.id
            || find_member(other.lobby,presence.platformId) == nullptr) continue;
        if (other.nativeFireteamDescriptorReady || other.bootstrapUnclaimed
            || (other.lobby.ownerPlatformId != bound->lobby.ownerPlatformId
                && other.lobby.ownerPlatformId != presence.platformId)
            || find_member(other.lobby,other.lobby.ownerPlatformId) == nullptr
            || find_member(other.lobby,bound->lobby.ownerPlatformId) == nullptr) return 0;
        return bound->lobby.id;
    }
    return 0;
}

[[nodiscard]] std::int32_t create_lobby(PresenceRecord& presence,
                                        const wire::Request& request,
                                        bool explicitNativeCreate = false) noexcept {
    if (request.valueA<0 || request.valueA>3 || request.valueB <= 0
        || request.valueB > static_cast<std::int32_t>(wire::kMemberCapacity)) {
        return kResultInvalidParameter;
    }
    if (presence.lobbyId != 0) {
        LobbyRecord* const existing = find_lobby(presence.lobbyId);
        if (existing != nullptr && existing->lobby.ownerPlatformId == presence.platformId
            && find_member(existing->lobby, presence.platformId) != nullptr) {
            if(existing->bootstrapUnclaimed) {
                if(lobby_count(presence.platformId,request.valueA,existing->lobby.id)
                    >= (request.valueA==3?2U:1U))return kResultLimitExceeded;
                existing->bootstrapUnclaimed=false;
                existing->lobby.type=request.valueA;
                existing->lobby.maximumMembers=request.valueB;
                ++existing->lobby.revision;
                publish_lobby_event(existing->lobby,wire::EventKind::lobbyChanged,
                    presence.platformId,existing->lobby.id);
                return kResultOk;
            }
            // native managed sessions ask for invisible type3 and each need a distinct session id
            if(request.valueA!=3 && existing->lobby.type!=3)return kResultOk;
        }
    }
    std::uint64_t retired = 0;
    if(lobby_count(presence.platformId,request.valueA)>=(request.valueA==3?2U:1U)) {
        retired = explicitNativeCreate && request.valueA==3
            ? superseded_foreign_fireteam(presence) : 0U;
        if(retired==0)return kResultLimitExceeded;
    }
    LobbyRecord* const record = allocate_lobby();
    if (record == nullptr) {
        return kResultLimitExceeded;
    }
    if (retired != 0) {
        if (!leave_lobby(presence,presence.platformId,retired)) return kResultFail;
        wire::Event departure{};
        departure.kind=wire::EventKind::memberChanged;
        departure.lobbyId=retired;
        departure.sourcePlatformId=presence.platformId;
        departure.memberPlatformId=presence.platformId;
        constexpr std::uint32_t left=2U;
        departure.payloadSize=sizeof(left);
        std::memcpy(departure.payload.data(),&left,sizeof(left));
        (void)queue_event(presence.platformId,departure);
    }
    std::uint64_t sequence = g_nextLobby++ & kLobbyAccountIdMask;
    if (sequence == 0) {
        sequence = g_nextLobby++ & kLobbyAccountIdMask;
    }
    wire::Lobby lobby{};
    lobby.id = kLobbyIdPrefix | sequence;
    lobby.ownerPlatformId = presence.platformId;
    lobby.revision = 1;
    lobby.type = request.valueA;
    lobby.maximumMembers = request.valueB;
    lobby.joinable = true;
    lobby.members[0].platformId = presence.platformId;
    lobby.members[0].accountSoid = presence.accountSoid;
    lobby.memberCount = 1;
    *record = {};
    record->lobby = lobby;
    record->occupied = true;
    presence.lobbyId = lobby.id;
    publish_lobby_event(lobby,
                        wire::EventKind::memberChanged,
                        presence.platformId,
                        presence.platformId);
    return kResultOk;
}

[[nodiscard]] std::int32_t ensure_solo_lobby(PresenceRecord& presence,
                                             const wire::Request& request) noexcept {
    if (request.valueB <= 0
        || request.valueB > static_cast<std::int32_t>(wire::kMemberCapacity)) {
        return kResultInvalidParameter;
    }
    if (presence.lobbyId != 0) {
        LobbyRecord* const existing = find_lobby(presence.lobbyId);
        if (existing != nullptr
            && find_member(existing->lobby, presence.platformId) != nullptr) {
            return kResultOk;
        }
        presence.lobbyId = 0;
    }
    const auto result=create_lobby(presence, request);
    if(result==kResultOk) {
        if(auto* record=find_lobby(presence.lobbyId))record->bootstrapUnclaimed=true;
    }
    return result;
}

[[nodiscard]] std::uint64_t superseded_activity_lobby(const PresenceRecord& presence,
                                                const LobbyRecord& target,
                                                bool matchedSocialActivity) noexcept {
    const auto* bound = find_lobby(presence.fireteamLobbyId);
    if (target.lobby.type != 3 || target.nativeFireteamDescriptorReady
        || bound == nullptr || bound->lobby.type != 3
        || !bound->nativeFireteamDescriptorReady
        || find_member(bound->lobby, presence.platformId) == nullptr
        || target.lobby.ownerPlatformId == presence.platformId) return 0;
    if (bound->lobby.ownerPlatformId != target.lobby.ownerPlatformId
        && !(matchedSocialActivity && bound->lobby.ownerPlatformId==presence.platformId)) return 0;
    std::uint64_t selected = 0;
    for (const auto& old : g_lobbies) {
        if (!old.occupied || old.lobby.type != 3 || old.nativeFireteamDescriptorReady
            || old.lobby.id == bound->lobby.id || old.lobby.id == target.lobby.id
            || find_member(old.lobby, presence.platformId) == nullptr) continue;
        // retirable: the caller's own solo activity, or an older one of the leader it now follows;
        // without this a member who followed once hit the two-activity cap (result 25)
        const bool ownSolo =
            old.lobby.ownerPlatformId == presence.platformId && old.lobby.memberCount == 1U;
        const bool leaderPrevious =
            old.lobby.ownerPlatformId == bound->lobby.ownerPlatformId
            && target.lobby.ownerPlatformId == bound->lobby.ownerPlatformId;
        if (!ownSolo && !leaderPrevious) continue;
        if (selected != 0) return 0;
        selected = old.lobby.id;
    }
    return selected;
}

[[nodiscard]] std::int32_t join_lobby(PresenceRecord& presence,
                                      const wire::Request& request,
                                      FriendshipCheck friendshipCheck,
                                      DiagnosticSink diagnosticSink) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    const PresenceRecord* const owner =
        record != nullptr ? find_presence(record->lobby.ownerPlatformId) : nullptr;
    if (!presence.gameplayReady || record == nullptr || !record->lobby.joinable
        || owner == nullptr || !owner->gameplayReady
        || !presence_fresh(*owner, presence.lastSeen)
        || find_member(record->lobby,owner->platformId)==nullptr) {
        return kResultAccessDenied;
    }
    // JoinLobby commits social membership, not a gameplay route. Retail calls it before our
    // group-zero descriptor publisher runs, so gating on that splits the travelling fireteam
    wire::Lobby& lobby = record->lobby;
    if (find_member(lobby, presence.platformId) != nullptr) {
        if (record->nativeFireteamDescriptorReady) {
            const auto previous = previous_native_fireteam(presence, *record);
            if (previous != 0 && !leave_replaced_lobby(presence, previous)) return kResultFail;
        }
        presence.lobbyId = lobby.id;
        if(record->nativeFireteamDescriptorReady)presence.fireteamLobbyId=lobby.id;
        return kResultOk;
    }
    const bool matchedSocialActivity=lobby.type==3 && !record->nativeFireteamDescriptorReady
        && dawn::state::matchmaking::social_search_native_lobby(presence.accountSoid,
            owner->accountSoid,owner->platformId,lobby.id);
    bool authorized = matchedSocialActivity;
    for (std::size_t index = 0; index < lobby.memberCount && !authorized; ++index) {
        authorized = accepted_friend(friendshipCheck,
                                     presence.platformId,
                                     lobby.members[index].platformId);
    }
    InviteRecord* matchingInvite = nullptr;
    for (InviteRecord& invite : g_invites) {
        if (invite.occupied && invite.lobbyId == lobby.id
            && invite.recipientPlatformId == presence.platformId) {
            matchingInvite = &invite;
            authorized = true;
            break;
        }
    }
    if (!authorized) {
        return kResultAccessDenied;
    }
    if (lobby.memberCount >= static_cast<std::size_t>(lobby.maximumMembers)
        || lobby.memberCount >= lobby.members.size()) {
        return kResultLimitExceeded;
    }
    auto retired = record->nativeFireteamDescriptorReady
        ? previous_native_fireteam(presence, *record) : 0U;
    if (lobby.type == 3 && lobby_count(presence.platformId, 3) >= 2U) {
        if (retired == 0)
            retired = superseded_activity_lobby(presence, *record, matchedSocialActivity);
        if (lobby_count(presence.platformId, 3) != 2U || retired == 0U)
            return kResultLimitExceeded;
    }
    if (retired != 0 && !leave_replaced_lobby(presence, retired)) return kResultFail;
    if(lobby.type!=3) {
        for(auto& previous:g_lobbies) {
            if(previous.occupied && previous.lobby.type!=3
                && find_member(previous.lobby,presence.platformId))
                (void)leave_lobby(presence,presence.platformId,previous.lobby.id);
        }
    }
    wire::Member& member = lobby.members[lobby.memberCount++];
    member.platformId = presence.platformId;
    member.accountSoid = presence.accountSoid;
    presence.lobbyId = lobby.id;
    if(record->nativeFireteamDescriptorReady)presence.fireteamLobbyId=lobby.id;
    if (matchingInvite != nullptr) {
        *matchingInvite = {};
    }
    ++lobby.revision;
    publish_lobby_event(lobby,
                        wire::EventKind::memberChanged,
                        presence.platformId,
                        presence.platformId);
    // callback 337 is the joiner's TARGET-join boundary; sending it to the owner makes the owner
    // join the newcomer and abandon its live fireteam
    (void)diagnosticSink;
    return kResultOk;
}

[[nodiscard]] std::int32_t invite(PresenceRecord& presence,
                                  const wire::Request& request,
                                  FriendshipCheck friendshipCheck) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    const PresenceRecord* const target = find_presence(request.targetPlatformId);
    if (!presence.gameplayReady || target == nullptr || !target->gameplayReady
        || !presence_fresh(*target, presence.lastSeen)
        || record == nullptr || find_member(record->lobby, presence.platformId) == nullptr
        || request.targetPlatformId == 0
        || !accepted_friend(friendshipCheck, presence.platformId, request.targetPlatformId)) {
        return kResultAccessDenied;
    }
    wire::Event event{};
    event.kind = wire::EventKind::lobbyInvite;
    event.lobbyId = record->lobby.id;
    event.sourcePlatformId = presence.platformId;
    event.memberPlatformId = request.targetPlatformId;
    event.payloadSize = request.payloadSize;
    std::copy_n(request.payload.begin(), request.payloadSize, event.payload.begin());
    InviteRecord* destination = nullptr;
    for (InviteRecord& invite : g_invites) {
        if (invite.occupied && invite.lobbyId == record->lobby.id
            && invite.recipientPlatformId == request.targetPlatformId) {
            destination = &invite;
            break;
        }
        if (!invite.occupied && destination == nullptr) {
            destination = &invite;
        }
    }
    if (destination == nullptr || !queue_event(request.targetPlatformId, event)) {
        return kResultFail;
    }
    *destination = {record->lobby.id,
                    presence.platformId,
                    request.targetPlatformId,
                    presence.lastSeen + kInviteLifetimeSeconds,
                    true};
    return kResultOk;
}

[[nodiscard]] std::int32_t set_lobby_data(PresenceRecord& presence,
                                          const wire::Request& request,
                                          bool remove) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    if (record == nullptr || record->lobby.ownerPlatformId != presence.platformId) {
        return kResultAccessDenied;
    }
    wire::Lobby& lobby = record->lobby;
    wire::Metadata* metadata = find_metadata(lobby.metadata, lobby.metadataCount, request.key);
    if (remove) {
        if (metadata == nullptr) {
            return kResultFail;
        }
        const std::size_t index = static_cast<std::size_t>(metadata - lobby.metadata.data());
        for (std::size_t current = index + 1; current < lobby.metadataCount; ++current) {
            lobby.metadata[current - 1] = lobby.metadata[current];
        }
        lobby.metadata[--lobby.metadataCount] = {};
    } else {
        if (metadata == nullptr) {
            if (lobby.metadataCount == lobby.metadata.size()) {
                return kResultLimitExceeded;
            }
            metadata = &lobby.metadata[lobby.metadataCount++];
            metadata->key = request.key;
        }
        metadata->value = request.value;
    }
    ++lobby.revision;
    publish_lobby_event(lobby,
                        wire::EventKind::lobbyChanged,
                        presence.platformId,
                        lobby.id);
    return kResultOk;
}

[[nodiscard]] std::int32_t set_member_data(PresenceRecord& presence,
                                           const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    wire::Member* const member =
        record == nullptr ? nullptr : find_member(record->lobby, presence.platformId);
    if (member == nullptr) {
        return kResultAccessDenied;
    }
    wire::Metadata* metadata = find_metadata(member->metadata, member->metadataCount, request.key);
    if (metadata == nullptr) {
        if (member->metadataCount == member->metadata.size()) {
            return kResultLimitExceeded;
        }
        metadata = &member->metadata[member->metadataCount++];
        metadata->key = request.key;
    }
    metadata->value = request.value;
    ++record->lobby.revision;
    publish_lobby_event(record->lobby,
                        wire::EventKind::lobbyChanged,
                        presence.platformId,
                        presence.platformId);
    return kResultOk;
}

[[nodiscard]] std::int32_t set_owner(PresenceRecord& presence,
                                     const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    const PresenceRecord* const target = find_presence(request.targetPlatformId);
    if (record == nullptr || record->lobby.ownerPlatformId != presence.platformId
        || target == nullptr || !target->gameplayReady
        || !presence_fresh(*target, presence.lastSeen)
        || find_member(record->lobby, request.targetPlatformId) == nullptr) {
        return kResultAccessDenied;
    }
    for (std::size_t index = 0; index < record->lobby.memberCount; ++index) {
        discard_gameplay_bootstrap_descriptor_events(
            record->lobby.members[index].platformId, record->lobby.id);
    }
    record->lobby.ownerPlatformId = request.targetPlatformId;
    record->nativeFireteamDescriptor = {};
    record->descriptorOwnerPlatformId = 0;
    record->nativeFireteamDescriptorReady = false;
    ++record->lobby.revision;
    publish_lobby_event(record->lobby,
                        wire::EventKind::lobbyChanged,
                        presence.platformId,
                        request.targetPlatformId);
    return kResultOk;
}

[[nodiscard]] std::int32_t set_joinable(PresenceRecord& presence,
                                        const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    if (record == nullptr || record->lobby.ownerPlatformId != presence.platformId
        || (request.valueA != 0 && request.valueA != 1)) {
        return kResultAccessDenied;
    }
    record->lobby.joinable = request.valueA != 0;
    ++record->lobby.revision;
    publish_lobby_event(record->lobby,
                        wire::EventKind::lobbyChanged,
                        presence.platformId,
                        record->lobby.id);
    return kResultOk;
}

[[nodiscard]] std::int32_t set_game_server(PresenceRecord& presence,
                                           const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    if (record == nullptr || record->lobby.ownerPlatformId != presence.platformId
        || request.valueB < 0 || request.valueB > (std::numeric_limits<std::uint16_t>::max)()) {
        return kResultAccessDenied;
    }
    record->lobby.gameServerPlatformId = request.targetPlatformId;
    record->lobby.gameServerIp = static_cast<std::uint32_t>(request.valueA);
    record->lobby.gameServerPort = static_cast<std::uint16_t>(request.valueB);
    ++record->lobby.revision;
    publish_lobby_event(record->lobby,
                        wire::EventKind::lobbyChanged,
                        presence.platformId,
                        record->lobby.id);
    return kResultOk;
}

[[nodiscard]] std::int32_t set_member_limit(PresenceRecord& presence,
                                            const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    if (record == nullptr || record->lobby.ownerPlatformId != presence.platformId
        || request.valueA <= 0
        || request.valueA > static_cast<std::int32_t>(wire::kMemberCapacity)
        || static_cast<std::size_t>(request.valueA) < record->lobby.memberCount) {
        return kResultAccessDenied;
    }
    record->lobby.maximumMembers = request.valueA;
    ++record->lobby.revision;
    publish_lobby_event(record->lobby,
                        wire::EventKind::lobbyChanged,
                        presence.platformId,
                        record->lobby.id);
    return kResultOk;
}

[[nodiscard]] std::int32_t set_lobby_type(PresenceRecord& presence,
                                          const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    if (record == nullptr || record->lobby.ownerPlatformId != presence.platformId
        || request.valueA<0 || request.valueA>3) {
        return kResultAccessDenied;
    }
    for(std::size_t i=0;i<record->lobby.memberCount;++i) {
        if(lobby_count(record->lobby.members[i].platformId,request.valueA,record->lobby.id)
            >= (request.valueA==3?2U:1U))return kResultLimitExceeded;
    }
    record->lobby.type = request.valueA;
    ++record->lobby.revision;
    publish_lobby_event(record->lobby,
                        wire::EventKind::lobbyChanged,
                        presence.platformId,
                        record->lobby.id);
    return kResultOk;
}

[[nodiscard]] std::int32_t send_event_payload(PresenceRecord& presence,
                                              const wire::Request& request,
                                              wire::EventKind kind,
                                              FriendshipCheck friendshipCheck) noexcept {
    if(kind==wire::EventKind::rendezvousBatch) {
        dawn::middleware::social::rendezvous_batch::View packets{};
        if(request.payloadSize>request.payload.size()
            || !dawn::middleware::social::rendezvous_batch::decode(
                std::span(request.payload).first(request.payloadSize),packets))return kResultInvalidParameter;
    }
    const bool rendezvousKind =
        kind == wire::EventKind::rendezvous || kind == wire::EventKind::rendezvousBatch;
    LobbyRecord* const record = find_lobby(request.lobbyId);
    const bool senderInRecord =
        record != nullptr && find_member(record->lobby, presence.platformId) != nullptr;
    if (request.payloadSize == 0 || (!rendezvousKind && !senderInRecord)) {
        return kResultAccessDenied;
    }
    wire::Event event{};
    event.kind = kind;
    event.lobbyId = senderInRecord ? record->lobby.id : 0;
    event.sourcePlatformId = presence.platformId;
    event.memberPlatformId = request.targetPlatformId;
    event.payloadSize = request.payloadSize;
    std::copy_n(request.payload.begin(), request.payloadSize, event.payload.begin());
    bool sent = false;
    if (rendezvousKind) {
        const auto* target=find_presence(request.targetPlatformId);
        const bool online = target != nullptr && presence.sessionGeneration != 0
                            && target->sessionGeneration != 0;
        const bool publicPeer=online
            && (dawn::state::activity::shared::public_peers(presence.accountSoid,target->accountSoid)
                || dawn::state::matchmaking::social_search_peers(presence.accountSoid,target->accountSoid));
        const auto shares = [&](std::uint64_t lobbyId) noexcept {
            const auto* candidate = lobbyId != 0 ? find_lobby(lobbyId) : nullptr;
            return candidate != nullptr
                   && find_member(candidate->lobby, presence.platformId) != nullptr
                   && find_member(candidate->lobby, request.targetPlatformId) != nullptr;
        };
        const bool sharedLobby = online
            && (shares(request.lobbyId) || shares(presence.lobbyId)
                || shares(presence.fireteamLobbyId) || shares(target->lobbyId)
                || shares(target->fireteamLobbyId));
        const bool friendPeer =
            online && accepted_friend(friendshipCheck, presence.platformId, request.targetPlatformId);
        if (request.targetPlatformId == 0 || !(sharedLobby || publicPeer || friendPeer)) {
            return kResultAccessDenied;
        }
        sent = queue_event(request.targetPlatformId, event);
    } else {
        for (std::size_t index = 0; index < record->lobby.memberCount; ++index) {
            sent = queue_event(record->lobby.members[index].platformId, event) || sent;
        }
    }
    return sent ? kResultOk : kResultFail;
}

[[maybe_unused, nodiscard]] std::int32_t publish_membership_projection(
    PresenceRecord& presence,
    const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    if (record == nullptr || presence.lobbyId != request.lobbyId
        || find_member(record->lobby, presence.platformId) == nullptr
        || record->lobby.ownerPlatformId == 0U
        || record->lobby.ownerPlatformId == presence.platformId
        || request.targetPlatformId != record->lobby.ownerPlatformId
        || request.payloadSize == 0U) {
        return kResultAccessDenied;
    }
    dawn::middleware::social::membership_projection::Metadata metadata{};
    const auto payload = std::span(request.payload).first(request.payloadSize);
    if (!dawn::middleware::social::membership_projection::inspect(payload, metadata)
        || metadata.memberCount != record->lobby.memberCount) {
        return kResultInvalidParameter;
    }

    for (const EventRecord& retained : g_events) {
        if (!retained.occupied
            || retained.recipientPlatformId != record->lobby.ownerPlatformId
            || retained.event.kind != wire::EventKind::membershipProjection
            || retained.event.lobbyId != record->lobby.id
            || retained.event.sourcePlatformId != presence.platformId) {
            continue;
        }
        dawn::middleware::social::membership_projection::Metadata retainedMetadata{};
        if (dawn::middleware::social::membership_projection::inspect(
                std::span(retained.event.payload).first(retained.event.payloadSize),
                retainedMetadata)
            && retainedMetadata.revision == metadata.revision) {
            return kResultOk;
        }
    }

    wire::Event event{};
    event.kind = wire::EventKind::membershipProjection;
    event.lobbyId = record->lobby.id;
    event.sourcePlatformId = presence.platformId;
    event.memberPlatformId = record->lobby.ownerPlatformId;
    event.payloadSize = request.payloadSize;
    std::copy_n(request.payload.begin(), request.payloadSize, event.payload.begin());
    return queue_event(record->lobby.ownerPlatformId, event) ? kResultOk : kResultFail;
}

[[nodiscard]] std::int32_t publish_native_fireteam_descriptor(
    PresenceRecord& presence,
    const wire::Request& request) noexcept {
    LobbyRecord* const record = find_lobby(request.lobbyId);
    if (record == nullptr || !presence.gameplayReady
        || record->lobby.ownerPlatformId != presence.platformId
        || find_member(record->lobby, presence.platformId) == nullptr
        || request.payloadSize != GameplayBootstrapDescriptor{}.size()) {
        return kResultAccessDenied;
    }

    GameplayBootstrapDescriptor descriptor{};
    std::copy_n(request.payload.begin(), descriptor.size(), descriptor.begin());
    if (!valid_native_fireteam_descriptor(descriptor, record->lobby.id)) {
        return kResultInvalidParameter;
    }
    const auto* bound=find_lobby(presence.fireteamLobbyId);
    if(bound && bound->lobby.id!=record->lobby.id
        && bound->lobby.ownerPlatformId!=presence.platformId)
        return kResultAccessDenied;
    const auto previous = previous_native_fireteam(presence, *record);
    if (previous != 0 && !leave_replaced_lobby(presence, previous)) return kResultFail;
    presence.fireteamLobbyId=record->lobby.id;
    if (record->nativeFireteamDescriptorReady
        && record->descriptorOwnerPlatformId == presence.platformId
        && record->nativeFireteamDescriptor == descriptor) {
        return kResultOk;
    }

    record->nativeFireteamDescriptor = descriptor;
    record->descriptorOwnerPlatformId = presence.platformId;
    record->nativeFireteamDescriptorReady = true;
    for (std::size_t index = 0; index < record->lobby.memberCount; ++index) {
        const std::uint64_t member = record->lobby.members[index].platformId;
        if (member != record->lobby.ownerPlatformId) {
            discard_gameplay_bootstrap_descriptor_events(member, record->lobby.id);
        }
    }
    return kResultOk;
}

} // namespace

void set_gameplay_bootstrap_descriptor_provider(
    GameplayBootstrapDescriptorProvider provider) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_gameplayBootstrapDescriptorProvider = provider;
    ReleaseSRWLockExclusive(&g_lock);
}

bool native_fireteam_descriptor(std::uint64_t lobbyId,
                                std::uint64_t ownerPlatformId,
                                GameplayBootstrapDescriptor& output) noexcept {
    output = {};
    if (lobbyId == 0 || ownerPlatformId == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    const LobbyRecord* const record = find_lobby(lobbyId);
    const PresenceRecord* const owner = find_presence(ownerPlatformId);
    const bool found = record != nullptr && owner != nullptr && owner->gameplayReady
                       && social_lobby_id(*owner) == lobbyId
                       && record->lobby.ownerPlatformId == ownerPlatformId
                       && find_member(record->lobby, ownerPlatformId) != nullptr
                       && record->nativeFireteamDescriptorReady
                       && record->descriptorOwnerPlatformId == ownerPlatformId
                       && valid_native_fireteam_descriptor(
                           record->nativeFireteamDescriptor, lobbyId);
    if (found) {
        output = record->nativeFireteamDescriptor;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

bool begin_game_session(std::uint64_t platformId,
                        std::uint64_t accountSoid,
                        std::uint64_t sessionGeneration,
                        std::uint64_t nowSeconds) noexcept {
    if (platformId == 0 || accountSoid == 0 || sessionGeneration == 0 || nowSeconds == 0) {
        return false;
    }

    AcquireSRWLockExclusive(&g_lock);
    expire(nowSeconds);
    PresenceRecord* record = find_presence(platformId);
    if (record != nullptr) {
        leave_all_lobbies(*record);
    }
    discard_session_events(platformId);
    discard_session_invites(platformId);
    if (record == nullptr) {
        record = touch(platformId, accountSoid, nowSeconds);
    }
    if (record != nullptr) {
        *record = {};
        record->platformId = platformId;
        record->accountSoid = accountSoid;
        record->sessionGeneration = sessionGeneration;
        record->lastSeen = nowSeconds;
        record->occupied = true;
        record->gameplayReady = false;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return record != nullptr;
}

bool mark_gameplay_ready(std::uint64_t platformId,
                         std::uint64_t accountSoid,
                         std::uint64_t sessionGeneration,
                         std::uint64_t nowSeconds) noexcept {
    if (platformId == 0 || accountSoid == 0 || sessionGeneration == 0 || nowSeconds == 0) {
        return false;
    }

    AcquireSRWLockExclusive(&g_lock);
    expire(nowSeconds);
    PresenceRecord* const record = find_presence(platformId);
    const bool exactGeneration = record != nullptr && record->accountSoid == accountSoid
                                 && record->sessionGeneration == sessionGeneration;
    if (exactGeneration) {
        record->lastSeen = nowSeconds;
        record->gameplayReady = true;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return exactGeneration;
}

bool lobby_for_member(std::uint64_t platformId, wire::Lobby& output) noexcept {
    output = {};
    if (platformId == 0) {
        return false;
    }

    AcquireSRWLockShared(&g_lock);
    const PresenceRecord* const presenceRecord = find_presence(platformId);
    const LobbyRecord* const lobbyRecord =
        presenceRecord != nullptr && presenceRecord->gameplayReady
                && social_lobby_id(*presenceRecord) != 0
            ? find_lobby(social_lobby_id(*presenceRecord))
            : nullptr;
    const PresenceRecord* const ownerRecord =
        lobbyRecord != nullptr ? find_presence(lobbyRecord->lobby.ownerPlatformId) : nullptr;
    const bool found = lobbyRecord != nullptr && ownerRecord != nullptr
                       && ownerRecord->gameplayReady
                       && social_lobby_id(*ownerRecord) == lobbyRecord->lobby.id
                       && lobbyRecord->lobby.ownerPlatformId != 0
                       && find_member(lobbyRecord->lobby, platformId) != nullptr
                       && find_member(lobbyRecord->lobby,
                                      lobbyRecord->lobby.ownerPlatformId) != nullptr;
    if (found) {
        output = lobbyRecord->lobby;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

bool lobby_for_account(std::uint64_t accountSoid, wire::Lobby& output) noexcept {
    output = {};
    if (accountSoid == 0) {
        return false;
    }
    std::uint64_t platformId = 0;
    AcquireSRWLockShared(&g_lock);
    for (const PresenceRecord& presence : g_presence) {
        if (presence.occupied && presence.accountSoid == accountSoid) {
            platformId = presence.platformId;
            break;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return platformId != 0 && lobby_for_member(platformId, output);
}

bool shares_fireteam(std::uint64_t leftPlatformId, std::uint64_t rightPlatformId) noexcept {
    if (leftPlatformId == 0 || rightPlatformId == 0 || leftPlatformId == rightPlatformId)
        return false;
    AcquireSRWLockShared(&g_lock);
    const bool shared = shares_fireteam_locked(find_presence(leftPlatformId),
                                               find_presence(rightPlatformId));
    ReleaseSRWLockShared(&g_lock);
    return shared;
}

bool shares_fireteam_accounts(std::uint64_t leftAccountSoid,
                              std::uint64_t rightAccountSoid) noexcept {
    if (leftAccountSoid == 0 || rightAccountSoid == 0 || leftAccountSoid == rightAccountSoid)
        return false;
    AcquireSRWLockShared(&g_lock);
    const PresenceRecord* left = nullptr;
    const PresenceRecord* right = nullptr;
    for (const auto& presence : g_presence) {
        if (!presence.occupied) continue;
        if (presence.accountSoid == leftAccountSoid) left = &presence;
        if (presence.accountSoid == rightAccountSoid) right = &presence;
    }
    const bool shared = shares_fireteam_locked(left, right);
    ReleaseSRWLockShared(&g_lock);
    return shared;
}

bool lobby_snapshot(std::uint64_t lobbyId, wire::Lobby& output) noexcept {
    output = {};
    if (lobbyId == 0) {
        return false;
    }

    AcquireSRWLockShared(&g_lock);
    const LobbyRecord* const record = find_lobby(lobbyId);
    const PresenceRecord* const owner =
        record != nullptr ? find_presence(record->lobby.ownerPlatformId) : nullptr;
    const bool found = record != nullptr && record->lobby.id == lobbyId
                       && owner != nullptr && owner->gameplayReady
                       && find_member(record->lobby,owner->platformId)!=nullptr
                       && record->lobby.ownerPlatformId != 0
                       && record->lobby.memberCount != 0;
    if (found) {
        output = record->lobby;
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

bool queue_owner_gameplay_admission(std::uint64_t lobbyId,
                                    std::uint64_t sourcePlatformId) noexcept {
    // kept for the retired dedicated-host path; telling a retail host to join its own descriptor broke loading
    (void)lobbyId;
    (void)sourcePlatformId;
    return false;
}

bool presence(std::uint64_t platformId,
              std::uint64_t nowSeconds,
              Presence& output) noexcept {
    output = {};
    if (platformId == 0 || nowSeconds == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    expire(nowSeconds);
    const PresenceRecord* const record = find_presence(platformId);
    if (record != nullptr && record->gameplayReady
        && presence_fresh(*record, nowSeconds)) {
        output.accountSoid = record->accountSoid;
        output.lobbyId = social_lobby_id(*record);
        output.online = true;
        const LobbyRecord* const lobby = find_lobby(output.lobbyId);
        const PresenceRecord* const owner =
            lobby != nullptr ? find_presence(lobby->lobby.ownerPlatformId) : nullptr;
        output.joinable = lobby != nullptr && lobby->lobby.joinable && owner != nullptr
                          && owner->gameplayReady && social_lobby_id(*owner) == lobby->lobby.id
                          && presence_fresh(*owner,nowSeconds)
                          && lobby->nativeFireteamDescriptorReady
                          && lobby->descriptorOwnerPlatformId == lobby->lobby.ownerPlatformId;
        output.status = record->status;
        output.steamDisplay = record->steamDisplay;
        output.connect = record->connect;
        output.playerGroup = record->playerGroup;
        output.playerGroupSize = record->playerGroupSize;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return output.online;
}

bool status_snapshot(std::uint64_t nowSeconds, StatusSnapshot& output) noexcept {
    output = {};
    if (nowSeconds == 0) return false;
    AcquireSRWLockExclusive(&g_lock);
    expire(nowSeconds);
    for (const PresenceRecord& record : g_presence) {
        if (!record.occupied || !record.gameplayReady || !presence_fresh(record, nowSeconds)
            || output.playerCount == output.players.size()) continue;
        StatusPlayer& player = output.players[output.playerCount++];
        player.platformId = record.platformId;
        player.accountSoid = record.accountSoid;
        player.lobbyId = social_lobby_id(record);
        player.status = record.status;
        player.steamDisplay = record.steamDisplay;
        const LobbyRecord* lobby = find_lobby(player.lobbyId);
        if (lobby != nullptr) {
            player.teamCount = (std::min)(lobby->lobby.memberCount,
                                          player.teamPlatformIds.size());
            for (std::size_t index = 0; index < player.teamCount; ++index) {
                player.teamPlatformIds[index] = lobby->lobby.members[index].platformId;
            }
        } else {
            player.teamCount = 1;
            player.teamPlatformIds[0] = player.platformId;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

bool remove_lobby_member(std::uint64_t lobbyId,
                         std::uint64_t actorPlatformId,
                         std::uint64_t targetPlatformId,
                         wire::Lobby* updatedLobby) noexcept {
    if (updatedLobby != nullptr) {
        *updatedLobby = {};
    }
    if (lobbyId == 0 || actorPlatformId == 0 || targetPlatformId == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    LobbyRecord* const record = find_lobby(lobbyId);
    PresenceRecord* const target = find_presence(targetPlatformId);
    const bool actorPresent = record != nullptr
                              && find_member(record->lobby, actorPlatformId) != nullptr;
    // the lobby row is the membership authority; presence may expire mid-request and made owner kicks fail
    const bool targetPresent = record != nullptr
                               && find_member(record->lobby, targetPlatformId) != nullptr;
    const bool authorized = actorPresent && targetPresent
                            && (actorPlatformId == targetPlatformId
                                || record->lobby.ownerPlatformId == actorPlatformId);
    bool removed = false;
    if (authorized) {
        const bool survivingLobby = record->lobby.memberCount > 1;
        if (target != nullptr) {
            removed = leave_lobby(*target, actorPlatformId,lobbyId);
        } else {
            wire::Lobby& lobby = record->lobby;
            std::size_t memberIndex = lobby.memberCount;
            for (std::size_t index = 0; index < lobby.memberCount; ++index) {
                if (lobby.members[index].platformId == targetPlatformId) {
                    memberIndex = index;
                    break;
                }
            }
            if (memberIndex != lobby.memberCount) {
                discard_gameplay_bootstrap_descriptor_events(targetPlatformId, lobbyId);
                for (std::size_t index = memberIndex + 1; index < lobby.memberCount; ++index) {
                    lobby.members[index - 1] = lobby.members[index];
                }
                lobby.members[lobby.memberCount - 1] = {};
                --lobby.memberCount;
                if (lobby.memberCount == 0) {
                    destroy_lobby(*record, actorPlatformId);
                } else {
                    if (lobby.ownerPlatformId == targetPlatformId) {
                        lobby.ownerPlatformId = lobby.members.front().platformId;
                        for (std::size_t index = 0; index < lobby.memberCount; ++index) {
                            discard_gameplay_bootstrap_descriptor_events(
                                lobby.members[index].platformId, lobby.id);
                        }
                        record->nativeFireteamDescriptor = {};
                        record->descriptorOwnerPlatformId = 0;
                        record->nativeFireteamDescriptorReady = false;
                    }
                    ++lobby.revision;
                    publish_lobby_event(
                        lobby, wire::EventKind::memberChanged, actorPlatformId, targetPlatformId);
                }
                removed = true;
            }
        }
        if (removed && survivingLobby) {
            if (updatedLobby != nullptr && record->occupied && record->lobby.id == lobbyId) {
                *updatedLobby = record->lobby;
            }
            wire::Event event{};
            event.kind = wire::EventKind::memberChanged;
            event.lobbyId = lobbyId;
            event.sourcePlatformId = actorPlatformId;
            event.memberPlatformId = targetPlatformId;
            const std::uint32_t left=2U;event.payloadSize=sizeof(left);
            std::memcpy(event.payload.data(),&left,sizeof(left));
            (void)queue_event(targetPlatformId, event);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return removed;
}

bool execute(std::uint64_t platformId,
             std::uint64_t accountSoid,
             std::uint64_t nowSeconds,
             const wire::Request& request,
             FriendshipCheck friendshipCheck,
             wire::Response& response,
             DiagnosticSink diagnosticSink) noexcept {
    response = {};
    response.result = kResultFail;
    if (platformId == 0 || accountSoid == 0 || nowSeconds == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    expire(nowSeconds);
    PresenceRecord* const current = touch(platformId, accountSoid, nowSeconds);
    if (current == nullptr) {
        response.result = kResultLimitExceeded;
    } else {
        if(request.operation==wire::Operation::queryLobby) {
            const auto id=request.lobbyId!=0?request.lobbyId:social_lobby_id(*current);
            const auto* record=find_lobby(id);
            response.result=record && find_member(record->lobby,platformId)?kResultOk:kResultAccessDenied;
            if(response.result==kResultOk)response.lobby=record->lobby;
            response.cursor=request.cursor;
            ReleaseSRWLockExclusive(&g_lock);
            return true;
        }
        const bool socialOperation = request.operation == wire::Operation::invite
                                     || request.operation == wire::Operation::joinLobby;
        struct SocialSnapshot final {
            unsigned selfReady{}, lobbyFound{}, member{}, joinable{}, descriptor{};
            unsigned otherFound{}, otherReady{}, otherFresh{}, otherInLobby{}, friendOk{};
            unsigned inviteHeld{};
            std::uint64_t other{};
        } social{};
        if (socialOperation) {
            const LobbyRecord* const lobbyRecord = find_lobby(request.lobbyId);
            social.selfReady = current->gameplayReady ? 1U : 0U;
            social.lobbyFound = lobbyRecord != nullptr ? 1U : 0U;
            if (lobbyRecord != nullptr) {
                social.member = find_member(lobbyRecord->lobby, platformId) != nullptr ? 1U : 0U;
                social.joinable = lobbyRecord->lobby.joinable ? 1U : 0U;
                social.descriptor = lobbyRecord->nativeFireteamDescriptorReady ? 1U : 0U;
            }
            social.other = request.operation == wire::Operation::invite
                               ? request.targetPlatformId
                               : (lobbyRecord != nullptr ? lobbyRecord->lobby.ownerPlatformId : 0);
            if (const PresenceRecord* const other = find_presence(social.other); other != nullptr) {
                social.otherFound = 1U;
                social.otherReady = other->gameplayReady ? 1U : 0U;
                social.otherFresh = presence_fresh(*other, current->lastSeen) ? 1U : 0U;
                social.otherInLobby =
                    lobbyRecord != nullptr && social_lobby_id(*other) == lobbyRecord->lobby.id ? 1U
                                                                                                : 0U;
            }
            social.friendOk =
                social.other != 0 && accepted_friend(friendshipCheck, platformId, social.other) ? 1U
                                                                                                 : 0U;
            for (const InviteRecord& held : g_invites) {
                if (held.occupied && held.lobbyId == request.lobbyId
                    && held.recipientPlatformId == platformId) {
                    social.inviteHeld = 1U;
                }
            }
        }
        const bool lifecycleOperation = request.operation == wire::Operation::createLobby
            || request.operation == wire::Operation::joinLobby
            || request.operation == wire::Operation::leaveLobby
            || request.operation == wire::Operation::publishNativeFireteamDescriptor;
        const auto priorFireteam = current->fireteamLobbyId;
        const auto priorInvisibleCount = lifecycleOperation ? lobby_count(platformId, 3) : 0U;
        switch (request.operation) {
        case wire::Operation::poll:
            response.result = kResultOk;
            if (std::strcmp(request.key.data(), kOwnerAdmissionDiagnosticPollKey.data()) == 0) {
                std::array<char, kDiagnosticLineCapacity> line{};
                const int written = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=owner_gameplay_admission stage=poll_diag result=received platform=%llu lobby=%llu cursor=%llu key=%s value=%s",
                    static_cast<unsigned long long>(platformId),
                    static_cast<unsigned long long>(current->lobbyId),
                    static_cast<unsigned long long>(request.cursor),
                    request.key.data(),
                    request.value.data());
                emit_diagnostic(diagnosticSink, DiagnosticLevel::info, line, written);
            }
            break;
        case wire::Operation::createLobby:
            response.result = create_lobby(*current, request, true);
            break;
        case wire::Operation::joinLobby:
            response.result =
                join_lobby(*current, request, friendshipCheck, diagnosticSink);
            break;
        case wire::Operation::leaveLobby:
            response.result = request.lobbyId!=0
                                      && leave_lobby(*current, platformId,request.lobbyId)
                                  ? kResultOk
                                  : kResultFail;
            break;
        case wire::Operation::invite:
            response.result = invite(*current, request, friendshipCheck);
            break;
        case wire::Operation::setLobbyData:
            response.result = set_lobby_data(*current, request, false);
            break;
        case wire::Operation::deleteLobbyData:
            response.result = set_lobby_data(*current, request, true);
            break;
        case wire::Operation::setMemberData:
            response.result = set_member_data(*current, request);
            break;
        case wire::Operation::setOwner:
            response.result = set_owner(*current, request);
            break;
        case wire::Operation::setJoinable:
            response.result = set_joinable(*current, request);
            break;
        case wire::Operation::sendChat:
            response.result = send_event_payload(
                *current, request, wire::EventKind::chatMessage, friendshipCheck);
            break;
        case wire::Operation::sendRendezvous:
        case wire::Operation::sendRendezvousBatch:
            response.result = send_event_payload(*current, request,
                request.operation==wire::Operation::sendRendezvousBatch
                    ?wire::EventKind::rendezvousBatch:wire::EventKind::rendezvous,
                friendshipCheck);
            {
                std::array<char, kDiagnosticLineCapacity> line{};
                const int written = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=realtime stage=send_rendezvous result=%d source=0x%016llX"
                    " target=0x%016llX lobby=0x%016llX payload_bytes=%zu",
                    response.result,
                    static_cast<unsigned long long>(platformId),
                    static_cast<unsigned long long>(request.targetPlatformId),
                    static_cast<unsigned long long>(request.lobbyId),
                    request.payloadSize);
                emit_diagnostic(diagnosticSink,
                                response.result == kResultOk ? DiagnosticLevel::info
                                                             : DiagnosticLevel::warning,
                                line,
                                written);
            }
            break;
        case wire::Operation::setGameServer:
            response.result = set_game_server(*current, request);
            break;
        case wire::Operation::setMemberLimit:
            response.result = set_member_limit(*current, request);
            break;
        case wire::Operation::setLobbyType:
            response.result = set_lobby_type(*current, request);
            break;
        case wire::Operation::setRichPresence:
            response.result = set_rich_presence(*current, request);
            break;
        case wire::Operation::clearRichPresence:
            clear_rich_presence(*current);
            response.result = kResultOk;
            break;
        case wire::Operation::ensureSoloLobby:
            response.result = ensure_solo_lobby(*current, request);
            break;
        case wire::Operation::publishMembershipProjection:
            response.result = kResultAccessDenied;
            break;
        case wire::Operation::publishNativeFireteamDescriptor:
            response.result = publish_native_fireteam_descriptor(*current, request);
            break;
        default:
            response.result = kResultInvalidParameter;
            break;
        }
        if (lifecycleOperation) {
            std::array<char, kDiagnosticLineCapacity> line{};
            const int written = std::snprintf(line.data(), line.size(),
                "ev=realtime stage=lifecycle operation=%u result=%d platform=0x%016llX "
                "requested=0x%016llX current=0x%016llX fireteam_before=0x%016llX "
                "fireteam_after=0x%016llX invisible_before=%zu invisible_after=%zu",
                static_cast<unsigned>(request.operation), response.result,
                static_cast<unsigned long long>(platformId),
                static_cast<unsigned long long>(request.lobbyId),
                static_cast<unsigned long long>(current->lobbyId),
                static_cast<unsigned long long>(priorFireteam),
                static_cast<unsigned long long>(current->fireteamLobbyId),
                priorInvisibleCount, lobby_count(platformId, 3));
            emit_diagnostic(diagnosticSink, response.result == kResultOk
                ? DiagnosticLevel::info : DiagnosticLevel::warning, line, written);
        }
        if (socialOperation) {
            std::array<char, kDiagnosticLineCapacity> line{};
            const int written = std::snprintf(
                line.data(),
                line.size(),
                "ev=realtime stage=%s result=%d platform=0x%016llX lobby=0x%016llX other=0x%016llX "
                "self_ready=%u lobby_found=%u member=%u joinable=%u descriptor=%u other_found=%u "
                "other_ready=%u other_fresh=%u other_in_lobby=%u friend=%u invite_held=%u",
                request.operation == wire::Operation::invite ? "invite" : "join",
                response.result,
                static_cast<unsigned long long>(platformId),
                static_cast<unsigned long long>(request.lobbyId),
                static_cast<unsigned long long>(social.other),
                social.selfReady,
                social.lobbyFound,
                social.member,
                social.joinable,
                social.descriptor,
                social.otherFound,
                social.otherReady,
                social.otherFresh,
                social.otherInLobby,
                social.friendOk,
                social.inviteHeld);
            emit_diagnostic(diagnosticSink, DiagnosticLevel::info, line, written);
        }
        if (current->lobbyId != 0) {
            if (LobbyRecord* const record = find_lobby(current->lobbyId); record != nullptr) {
                (void)ensure_gameplay_bootstrap_descriptor(*record, platformId);
            }
        }
        copy_current_lobby(*current, response);
        if(response.result==kResultOk && (request.operation==wire::Operation::createLobby
            || request.operation==wire::Operation::joinLobby)) {
            if(const auto* target=find_lobby(current->lobbyId))response.lobby=target->lobby;
        }
        (void)copy_events(platformId, request.cursor, response, diagnosticSink);
    }
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_gameplayBootstrapDescriptorProvider = nullptr;
    g_lobbies = {};
    g_presence = {};
    g_events = {};
    g_invites = {};
    g_nextLobby = 1;
    g_nextEvent = 1;
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace restoration::realtime_store
