#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

#include "middleware/datagen/definitions.h"
#include "middleware/datagen/family6/family6_encoder.h"
#include "middleware/datagen/social_roster/social_roster_encoder.h"
#include "middleware/social/realtime_codec.h"

namespace {

template <typename Value>
[[nodiscard]] Value read(const std::byte* source) noexcept {
    Value value{};
    std::memcpy(&value, source, sizeof value);
    return value;
}

[[nodiscard]] bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

} // namespace

int main() {
    namespace datagen = dawn::middleware::datagen;
    namespace family6 = datagen::family6;
    namespace roster = datagen::social_roster;

    constexpr std::uint64_t kAccountSoid = 0x9EAA000100200100ULL;
    constexpr std::uint64_t kJoinerAccountSoid = 0x9EAA000100100100ULL;
    constexpr std::uint64_t kCharacterSoid = 0x9EAA000100200102ULL;
    constexpr std::uint64_t kSteamLobbyId = 0x0184000000000002ULL;
    constexpr family6::GraphKeys kGraph = family6::account_linked_graph(kAccountSoid);
    static_assert(family6::valid_graph(kGraph, kSteamLobbyId));

    std::array<std::byte, datagen::kSocialRosterDirectorySize> directory{};
    std::array<std::byte, datagen::kSocialRosterMemberSize> rosterMember{};
    std::array<std::byte, family6::kAnchorSize> anchor{};
    std::array<std::byte, family6::kPartySize> party{};

    const roster::Member social{kAccountSoid, 1060, 1273};
    family6::Member partyOwner{};
    partyOwner.accountSoid = kAccountSoid;
    partyOwner.emblemDefinition = 1273;
    partyOwner.emblemVariant = 0xFFFFU;
    std::memcpy(partyOwner.displayName.data(), "PlayerTwo", 11);
    partyOwner.status = family6::kFireteamMemberStatus | family6::kPartyLeaderStatus;
    family6::Member partyJoiner{};
    partyJoiner.accountSoid = kJoinerAccountSoid;
    partyJoiner.emblemDefinition = 1273;
    partyJoiner.emblemVariant = 0xFFFFU;
    std::memcpy(partyJoiner.displayName.data(), "PlayerOne", 9);
    partyJoiner.status = family6::kFireteamMemberStatus;
    const std::array partyMembers{partyOwner, partyJoiner};

    bool passed = true;
    passed &= require(roster::encode_directory(kAccountSoid, kCharacterSoid, directory),
                      "encode Family-2 slot zero");
    passed &= require(roster::encode_member(social, rosterMember),
                      "encode Family-2 slot one");
    passed &= require(family6::encode_anchor(kGraph.accountRootSoid,
                                             kGraph.family6AnchorLinkSoid,
                                             anchor),
                      "encode Family-6 slot zero");
    passed &= require(family6::encode_party(kGraph.family6PartyLeadingSoid,
                                            partyMembers,
                                            party),
                      "encode Family-6 slot one");

    const std::uint64_t family2Root = read<std::uint64_t>(directory.data());
    const std::uint64_t family2DirectoryLink =
        read<std::uint64_t>(directory.data() + sizeof(std::uint64_t));
    const std::uint64_t family2MemberLeading = read<std::uint64_t>(rosterMember.data());
    const std::uint64_t family2SelectedCharacter =
        read<std::uint64_t>(directory.data() + roster::kSelectedCharacterSoidOffset);
    const std::uint64_t family6Root = read<std::uint64_t>(anchor.data());
    const std::uint64_t family6AnchorLink =
        read<std::uint64_t>(anchor.data() + sizeof(std::uint64_t));
    const std::uint64_t family6PartyLeading = read<std::uint64_t>(party.data());

    passed &= require(family2Root == kGraph.accountRootSoid, "Family-2 root key");
    passed &= require(family2DirectoryLink == kGraph.family2DirectoryLinkSoid,
                      "Family-2 slot-zero link");
    passed &= require(family2MemberLeading == kGraph.family2MemberObjectSoid,
                      "Family-2 slot-one key/body");
    passed &= require(family2SelectedCharacter == kCharacterSoid,
                      "Family-2 dedicated selected-character field");
    const auto memberCapacity = dawn::middleware::social::realtime::kMemberCapacity;
    passed &= require(
        read<std::uint8_t>(rosterMember.data() + roster::kJoinCapacityExternalOffset)
                == memberCapacity
            && read<std::uint8_t>(rosterMember.data() + roster::kJoinCapacityMatchedOffset)
                   == memberCapacity
            && read<std::uint8_t>(rosterMember.data() + roster::kJoinCapacityOffset)
                   == memberCapacity,
        "Family-2 slot-one native join capacity views");
    passed &= require(read<std::uint8_t>(rosterMember.data() + roster::kJoinPrivacyOffset) == 0U
                          && read<std::uint8_t>(rosterMember.data()
                                                + roster::kJoinClosedReasonsOffset)
                                 == 0U,
                      "Family-2 privacy and closed-reason bytes remain open");
    passed &= require(family6Root == kGraph.accountRootSoid, "Family-6 root key");
    passed &= require(family6AnchorLink == kGraph.family6AnchorLinkSoid,
                      "Family-6 slot-zero link");
    passed &= require(family6PartyLeading == kGraph.family6PartyLeadingSoid,
                      "Family-6 slot-one leading key");
    passed &= require(kGraph.family6PartyObjectSoid == kAccountSoid,
                      "Family-6 slot-one QueueZ object key");
    passed &= require(family2DirectoryLink == family2MemberLeading
                          && family2MemberLeading == family6AnchorLink
                          && family6AnchorLink == family6PartyLeading
                          && family6PartyLeading == kGraph.family6PartyObjectSoid,
                      "cross-family account-key graph");
    passed &= require(family6AnchorLink != kSteamLobbyId
                          && family6PartyLeading != kSteamLobbyId
                          && kGraph.family6PartyObjectSoid != kSteamLobbyId,
                      "Steam lobby excluded from QueueZ graph");
    passed &= require(read<std::uint64_t>(party.data() + 8) == kAccountSoid,
                      "Family-6 owner member SOID");
    passed &= require(read<std::uint32_t>(party.data() + 0x94) == 9U,
                      "Family-6 owner status");
    passed &= require(read<std::uint64_t>(party.data() + 0x98) == kJoinerAccountSoid,
                      "Family-6 joining member SOID");
    passed &= require(read<std::uint32_t>(party.data() + 0x124)
                          == family6::kFireteamMemberStatus,
                      "Family-6 joining member status without leader bit");
    for (std::size_t offset = 0x128; offset < party.size(); ++offset) {
        if (party[offset] != std::byte{}) {
            passed &= require(false, "Family-6 unused members/activity tail remain zero");
            break;
        }
    }

    if (passed) {
        std::puts("PASS: QueueZ Family-2/Family-6 account-link graph contract");
        return 0;
    }
    return 1;
}
