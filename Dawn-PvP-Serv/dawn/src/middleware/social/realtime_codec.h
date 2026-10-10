#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::social::realtime {

inline constexpr std::size_t kLobbyCapacity = 64;
// players per lobby and gameplay session, host row included; raised from 6 for 12-player fireteams.
// a lobby wider than a peer's capacity fails to decode there, so server and client DLLs ship together
inline constexpr std::size_t kMemberCapacity = 12;
inline constexpr std::size_t kMetadataCapacity = 8;
inline constexpr std::size_t kKeyCapacity = 64;
inline constexpr std::size_t kValueCapacity = 256;
inline constexpr std::size_t kEventCapacity = 4;
inline constexpr std::size_t kEventPayloadCapacity = 4096;
inline constexpr std::size_t kMaximumRequestSize = 8 * 1024;
inline constexpr std::size_t kMaximumResponseSize = 48 * 1024;

enum class Operation : std::uint16_t {
    poll = 1,
    createLobby = 2,
    joinLobby = 3,
    leaveLobby = 4,
    invite = 5,
    setLobbyData = 6,
    deleteLobbyData = 7,
    setMemberData = 8,
    setOwner = 9,
    setJoinable = 10,
    sendChat = 11,
    sendRendezvous = 12,
    setGameServer = 13,
    setMemberLimit = 14,
    setLobbyType = 15,
    setRichPresence = 16,
    clearRichPresence = 17,
    ensureSoloLobby = 18,
    // retired split-session workaround; production rejects it
    publishMembershipProjection = 19,
    publishNativeFireteamDescriptor = 20,
    queryLobby = 21,
    sendRendezvousBatch = 22,
};

enum class EventKind : std::uint16_t {
    lobbyInvite = 1,
    lobbyChanged = 2,
    memberChanged = 3,
    chatMessage = 4,
    rendezvous = 5,
    lobbyDestroyed = 6,
    ownerGameplayAdmission = 7,
    gameplayBootstrapDescriptor = 8,
    // retired split-session workaround; production does not emit it
    membershipProjection = 9,
    rendezvousBatch = 10,
};

struct Metadata final {
    std::array<char, kKeyCapacity> key{};
    std::array<char, kValueCapacity> value{};
};

struct Member final {
    std::uint64_t platformId{};
    std::uint64_t accountSoid{};
    std::array<Metadata, kMetadataCapacity> metadata{};
    std::size_t metadataCount{};
};

struct Lobby final {
    std::uint64_t id{};
    std::uint64_t ownerPlatformId{};
    std::uint64_t revision{};
    std::int32_t type{};
    std::int32_t maximumMembers{};
    bool joinable{};
    std::uint64_t gameServerPlatformId{};
    std::uint32_t gameServerIp{};
    std::uint16_t gameServerPort{};
    std::array<Metadata, kMetadataCapacity> metadata{};
    std::size_t metadataCount{};
    std::array<Member, kMemberCapacity> members{};
    std::size_t memberCount{};
};

struct Request final {
    Operation operation{Operation::poll};
    std::uint64_t cursor{};
    std::uint64_t lobbyId{};
    std::uint64_t targetPlatformId{};
    std::int32_t valueA{};
    std::int32_t valueB{};
    std::array<char, kKeyCapacity> key{};
    std::array<char, kValueCapacity> value{};
    std::array<std::byte, kEventPayloadCapacity> payload{};
    std::size_t payloadSize{};
};

struct Event final {
    std::uint64_t sequence{};
    EventKind kind{EventKind::lobbyChanged};
    std::uint64_t lobbyId{};
    std::uint64_t sourcePlatformId{};
    std::uint64_t memberPlatformId{};
    std::array<std::byte, kEventPayloadCapacity> payload{};
    std::size_t payloadSize{};
};

struct Response final {
    // Steam-compatible EResult; 1 is success
    std::int32_t result{};
    std::uint64_t cursor{};
    Lobby lobby{};
    std::array<Event, kEventCapacity> events{};
    std::size_t eventCount{};
};

[[nodiscard]] bool encode_request(const Request& request,
                                  std::span<std::byte> output,
                                  std::size_t& written) noexcept;
[[nodiscard]] bool decode_request(std::span<const std::byte> input,
                                  Request& request) noexcept;
[[nodiscard]] bool encode_response(const Response& response,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;
[[nodiscard]] bool decode_response(std::span<const std::byte> input,
                                   Response& response) noexcept;

} // namespace dawn::middleware::social::realtime
