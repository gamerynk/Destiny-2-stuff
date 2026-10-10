#include <array>
#include <cstdint>
#include <cstdio>
#include <span>

#include "middleware/gameplay/group/session_messages.h"
#include "server/gameplay/group/central_lobby_identity.h"
#include "server/gameplay/endpoint/gameplay_endpoint.h"

namespace {

namespace identity = dawn::server::gameplay::group::central_lobby_identity;
namespace endpoint = dawn::server::gameplay::endpoint;
namespace group = dawn::middleware::gameplay::group;
namespace realtime = dawn::middleware::social::realtime;

constexpr std::uint64_t kMainPlatform = 0x0110000100000001ULL;
constexpr std::uint64_t kUniversalMainPlatform = 0x0110000100000103ULL;
constexpr std::uint64_t kOwnerPlatform = 0x0110000100000101ULL;
constexpr std::uint64_t kThirdPlatform = 0x0110000100000003ULL;

[[nodiscard]] realtime::Lobby two_member_lobby() noexcept {
    realtime::Lobby lobby{};
    lobby.id = 0x0184000000000001ULL;
    lobby.ownerPlatformId = kOwnerPlatform;
    lobby.maximumMembers = 12;
    lobby.members[0].platformId = kOwnerPlatform;
    lobby.members[0].accountSoid = 0x9EAA000100200100ULL;
    lobby.members[1].platformId = kMainPlatform;
    lobby.members[1].accountSoid = 0x9EAA000100100100ULL;
    lobby.memberCount = 2;
    return lobby;
}

[[nodiscard]] bool expect(identity::Result actual,
                          identity::Result expected,
                          std::uint64_t platform,
                          std::uint64_t expectedPlatform,
                          const char* test) noexcept {
    if (actual == expected && platform == expectedPlatform) {
        return true;
    }
    std::fprintf(stderr,
                 "%s failed: result=%s expected=%s platform=0x%016llX expected=0x%016llX\n",
                 test,
                 identity::name(actual),
                 identity::name(expected),
                 static_cast<unsigned long long>(platform),
                 static_cast<unsigned long long>(expectedPlatform));
    return false;
}

[[nodiscard]] bool synthesized_owner_identity_is_exact() noexcept {
    group::MembershipPlayer owner{};
    if (!group::set_synthesized_pc_platform_identity(owner, kOwnerPlatform)
        || !owner.identityPresent) {
        return false;
    }
    std::uint64_t decoded = 0;
    for (std::size_t byte = 0; byte < sizeof(decoded); ++byte) {
        decoded |= std::to_integer<std::uint64_t>(owner.identity[byte]) << (byte * 8U);
    }
    bool zeroTail = true;
    for (std::size_t byte = sizeof(decoded); byte < owner.identity.size(); ++byte) {
        zeroTail &= owner.identity[byte] == std::byte{};
    }

    owner.identity.fill(std::byte{0xFF});
    owner.identityPresent = true;
    const bool rejectedEmpty = !group::set_synthesized_pc_platform_identity(owner, 0);
    bool cleared = !owner.identityPresent;
    for (const std::byte value : owner.identity) {
        cleared &= value == std::byte{};
    }
    return decoded == kOwnerPlatform && zeroTail && rejectedEmpty && cleared;
}

[[nodiscard]] bool per_lobby_machine_identity_is_exact() noexcept {
    constexpr std::uint64_t kEndpointMachine = 0x0123456789ABCDEFULL;
    constexpr std::uint64_t kLobbyA = 0x0184000000000001ULL;
    constexpr std::uint64_t kLobbyB = 0x0184000000000002ULL;
    constexpr std::uint64_t kExpectedA = 0xB7CA0F55583920EDULL;
    constexpr std::uint64_t kExpectedB = 0x88601EAAB07241DAULL;

    static_assert(endpoint::derive_lobby_machine_id(0, kLobbyA) == 0);
    static_assert(endpoint::derive_lobby_machine_id(kEndpointMachine, 0) == 0);
    static_assert(endpoint::derive_lobby_machine_id(kEndpointMachine, kLobbyA)
                  == kExpectedA);
    static_assert(endpoint::derive_lobby_machine_id(kEndpointMachine, kLobbyB)
                  == kExpectedB);
    static_assert(endpoint::recover_lobby_id(0, kExpectedA) == 0);
    static_assert(endpoint::recover_lobby_id(kEndpointMachine, 0) == 0);
    static_assert(endpoint::recover_lobby_id(kEndpointMachine, kExpectedA) == kLobbyA);
    static_assert(endpoint::recover_lobby_id(kEndpointMachine, kExpectedB) == kLobbyB);
    static_assert(endpoint::recover_lobby_id(kEndpointMachine, kExpectedA) != kLobbyB);
    static_assert(endpoint::recover_lobby_id(kEndpointMachine, kExpectedB) != kLobbyA);

    constexpr std::array<std::uint64_t, 8> lobbies{
        1,
        2,
        kLobbyA,
        kLobbyB,
        0x018400007FFFFFFFULL,
        0x01840000FFFFFFFFULL,
        0x7FFFFFFFFFFFFFFFULL,
        0xFFFFFFFFFFFFFFFFULL,
    };
    std::array<std::uint64_t, lobbies.size()> derived{};
    for (std::size_t index = 0; index < lobbies.size(); ++index) {
        derived[index] = endpoint::derive_lobby_machine_id(kEndpointMachine, lobbies[index]);
        if (derived[index] == 0
            || derived[index]
                   != endpoint::derive_lobby_machine_id(kEndpointMachine, lobbies[index])
            || endpoint::recover_lobby_id(kEndpointMachine, derived[index]) != lobbies[index]) {
            return false;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (derived[index] == derived[earlier]) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool gameplay_session_selects_exact_lobby_identity() noexcept {
    constexpr std::uint64_t kEndpointMachine = 0x0123456789ABCDEFULL;
    realtime::Lobby ownerSolo{};
    ownerSolo.id = 0x0184000000000002ULL;
    ownerSolo.ownerPlatformId = kOwnerPlatform;
    ownerSolo.maximumMembers = 12;
    ownerSolo.members[0].platformId = kOwnerPlatform;
    ownerSolo.members[0].accountSoid = 0x9EAA000100200100ULL;
    ownerSolo.memberCount = 1;

    const std::uint64_t ownerSession =
        endpoint::derive_lobby_machine_id(kEndpointMachine, ownerSolo.id);
    std::uint64_t platform = 0;
    identity::Result result = identity::resolve_for_session(kEndpointMachine,
                                                            ownerSession,
                                                            ownerSolo,
                                                            {},
                                                            kMainPlatform,
                                                            platform);
    bool ok = expect(result,
                     identity::Result::resolvedOnlyRemaining,
                     platform,
                     kOwnerPlatform,
                     "fixed main identity cannot capture friend solo session");

    realtime::Lobby joined = ownerSolo;
    joined.members[1].platformId = kMainPlatform;
    joined.members[1].accountSoid = 0x9EAA000100100100ULL;
    joined.memberCount = 2;
    const std::array<std::uint64_t, 1> ownerBound{kOwnerPlatform};
    platform = 0;
    result = identity::resolve_for_session(kEndpointMachine,
                                           ownerSession,
                                           joined,
                                           ownerBound,
                                           kMainPlatform,
                                           platform);
    ok &= expect(result,
                 identity::Result::resolvedPreferred,
                 platform,
                 kMainPlatform,
                 "joiner binds only remaining account in target lobby");

    realtime::Lobby wrongLobby = joined;
    wrongLobby.id = 0x0184000000000003ULL;
    platform = 0;
    result = identity::resolve_for_session(kEndpointMachine,
                                           ownerSession,
                                           wrongLobby,
                                           {},
                                           kMainPlatform,
                                           platform);
    ok &= expect(result,
                 identity::Result::invalidLobby,
                 platform,
                 0,
                 "native session cannot bind an unrelated lobby");

    realtime::Lobby universalJoined = joined;
    universalJoined.members[1].platformId = kUniversalMainPlatform;
    platform = 0;
    result = identity::resolve_joiner_for_session(kEndpointMachine,
                                                  ownerSession,
                                                  universalJoined,
                                                  {},
                                                  kMainPlatform,
                                                  platform);
    ok &= expect(result,
                 identity::Result::resolvedOnlyNonOwner,
                 platform,
                 kUniversalMainPlatform,
                 "central join binds the unique authenticated non-owner");
    return ok;
}

} // namespace

int main() {
    bool ok = synthesized_owner_identity_is_exact()
              && per_lobby_machine_identity_is_exact()
              && gameplay_session_selects_exact_lobby_identity();
    std::uint64_t platform = 0;
    realtime::Lobby lobby = two_member_lobby();

    const auto check = [&platform, &ok](const realtime::Lobby& candidate,
                                        std::span<const std::uint64_t> bound,
                                        std::uint64_t preferred,
                                        identity::Result expected,
                                        std::uint64_t expectedPlatform,
                                        const char* test) noexcept {
        platform = 0;
        const identity::Result actual = identity::resolve(candidate, bound, preferred, platform);
        ok &= expect(actual, expected, platform, expectedPlatform, test);
    };

    check(lobby,
          {},
          kMainPlatform,
          identity::Result::resolvedPreferred,
          kMainPlatform,
          "first endpoint uses validated preferred member");

    const std::array<std::uint64_t, 1> mainBound{kMainPlatform};
    check(lobby,
          mainBound,
          kMainPlatform,
          identity::Result::resolvedOnlyRemaining,
          kOwnerPlatform,
          "second endpoint cannot duplicate fixed main identity");
    check(lobby,
          mainBound,
          kOwnerPlatform,
          identity::Result::resolvedPreferred,
          kOwnerPlatform,
          "owner retry keeps its stable mapping");

    const std::array<std::uint64_t, 2> bothBound{kMainPlatform, kOwnerPlatform};
    check(lobby,
          bothBound,
          kMainPlatform,
          identity::Result::noRemainingMember,
          0,
          "third endpoint is not assigned a duplicate");

    lobby.members[2].platformId = kThirdPlatform;
    lobby.members[2].accountSoid = 0x9EAA000100300100ULL;
    lobby.memberCount = 3;
    check(lobby,
          mainBound,
          kMainPlatform,
          identity::Result::ambiguous,
          0,
          "arrival order does not choose between two unbound members");

    lobby = two_member_lobby();
    lobby.members[1].platformId = kOwnerPlatform;
    check(lobby,
          {},
          kOwnerPlatform,
          identity::Result::invalidLobby,
          0,
          "duplicate authoritative roster is rejected");

    lobby = two_member_lobby();
    lobby.ownerPlatformId = kThirdPlatform;
    check(lobby,
          {},
          kMainPlatform,
          identity::Result::invalidLobby,
          0,
          "owner missing from roster is rejected");

    lobby = two_member_lobby();
    const std::array<std::uint64_t, 1> staleBound{kThirdPlatform};
    check(lobby,
          staleBound,
          kMainPlatform,
          identity::Result::invalidBinding,
          0,
          "bound endpoint outside authoritative roster is rejected");

    if (!ok) {
        return 1;
    }
    std::puts("PASS: authoritative lobby members and per-lobby machine identities are exact");
    return 0;
}
