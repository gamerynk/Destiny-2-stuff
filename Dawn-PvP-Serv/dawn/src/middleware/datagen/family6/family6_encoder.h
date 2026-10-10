#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::datagen::family6 {

inline constexpr std::size_t kAnchorSize = 0x10;
inline constexpr std::size_t kPartySize = 0xB68;
inline constexpr std::size_t kMemberCapacity = 20;
inline constexpr std::size_t kMemberSize = 0x90;
inline constexpr std::size_t kDisplayNameCapacity = 64;

inline constexpr std::uint32_t kFireteamMemberStatus = 1U;
inline constexpr std::uint32_t kPartyLeaderStatus = 8U;

struct Member final {
    std::uint64_t accountSoid{};
    std::uint16_t emblemDefinition{0xFFFFU};
    std::uint16_t emblemVariant{0xFFFFU};
    std::array<char, kDisplayNameCapacity> displayName{};
    std::uint32_t status{};
};

// canonical QueueZ graph keys shared by the Family-2 social record and Family-6 fireteam record.
// the Steam lobby id is deliberately not part of this account-keyed graph
struct GraphKeys final {
    std::uint64_t accountRootSoid{};
    std::uint64_t family2DirectoryLinkSoid{};
    std::uint64_t family2MemberObjectSoid{};
    std::uint64_t family6AnchorLinkSoid{};
    std::uint64_t family6PartyLeadingSoid{};
    std::uint64_t family6PartyObjectSoid{};
};

[[nodiscard]] constexpr GraphKeys account_linked_graph(std::uint64_t accountSoid) noexcept {
    return {accountSoid, accountSoid, accountSoid, accountSoid, accountSoid, accountSoid};
}

[[nodiscard]] constexpr bool valid_graph(const GraphKeys& graph,
                                         std::uint64_t steamLobbyId) noexcept {
    return graph.accountRootSoid != 0 && steamLobbyId != 0
           && graph.accountRootSoid != steamLobbyId
           && graph.family2DirectoryLinkSoid == graph.accountRootSoid
           && graph.family2MemberObjectSoid == graph.accountRootSoid
           && graph.family6AnchorLinkSoid == graph.accountRootSoid
           && graph.family6PartyLeadingSoid == graph.accountRootSoid
           && graph.family6PartyObjectSoid == graph.accountRootSoid;
}

[[nodiscard]] bool encode_anchor(std::uint64_t accountSoid,
                                 std::uint64_t linkedAccountSoid,
                                 std::span<std::byte> destination) noexcept;

[[nodiscard]] bool encode_party(std::uint64_t linkedAccountSoid,
                                std::span<const Member> members,
                                std::span<std::byte> destination) noexcept;

} // namespace dawn::middleware::datagen::family6
