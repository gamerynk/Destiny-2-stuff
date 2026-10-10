#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::state::social {

inline constexpr std::size_t kRemoteCapacity = 100;
// kept small on purpose: a cached profile is a whole AccountState, and sizing this by
// kRemoteCapacity cost hundreds of MB of static storage. a miss only costs a re-download
inline constexpr std::size_t kProfileCacheCapacity = 8;
inline constexpr std::size_t kNameCapacity = 64;
inline constexpr std::size_t kLocationCapacity = 64;
inline constexpr std::size_t kSteamDisplayCapacity = 64;
inline constexpr std::size_t kConnectCapacity = 96;
inline constexpr std::size_t kPlayerGroupCapacity = 32;
inline constexpr std::size_t kPlayerGroupSizeCapacity = 16;

// values match Steamworks EFriendRelationship
enum class Relationship : std::int32_t {
    none = 0,
    blocked = 1,
    requestRecipient = 2,
    friendAccount = 3,
    requestInitiator = 4,
    ignored = 5,
    ignoredFriend = 6,
};

struct RemoteAccount final {
    std::uint64_t platformId{};
    std::uint64_t accountSoid{};
    std::uint64_t selectedCharacterSoid{};
    std::uint64_t shipInstanceSoid{};
    std::uint64_t lobbyId{};
    std::uint64_t profileRevision{};
    Relationship relationship{Relationship::none};
    std::int32_t personaState{};
    std::uint32_t appId{};
    std::uint32_t shipDefinitionHash{};
    std::int32_t power{};
    std::uint16_t emblemDefinition{0xFFFFU};
    std::uint16_t emblemVariant{0xFFFFU};
    std::uint8_t characterLevel{};
    std::uint8_t characterClass{};
    bool joinable{};
    std::array<char, kNameCapacity> displayName{};
    // D2's "status" rich-presence key
    std::array<char, kLocationCapacity> location{};
    std::array<char, kSteamDisplayCapacity> steamDisplay{};
    // D2's /connect: token, not Steam's command-line +connect_lobby form
    std::array<char, kConnectCapacity> connect{};
    std::array<char, kPlayerGroupCapacity> playerGroup{};
    std::array<char, kPlayerGroupSizeCapacity> playerGroupSize{};
};

struct Snapshot final {
    std::uint64_t localPlatformId{};
    std::uint64_t revision{};
    std::array<char, kNameCapacity> localDisplayName{};
    std::array<RemoteAccount, kRemoteCapacity> remotes{};
    std::size_t remoteCount{};
};

[[nodiscard]] bool valid(const Snapshot& snapshot) noexcept;

} // namespace dawn::state::social
