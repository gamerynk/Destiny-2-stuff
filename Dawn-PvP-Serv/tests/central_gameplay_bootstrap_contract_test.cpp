#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "middleware/gameplay/descriptor/join_descriptor.h"
#include "steam/runtime/social/central_gameplay_bootstrap.h"

namespace {

namespace bootstrap = dawn::steam::runtime::social::central_gameplay_bootstrap;
namespace descriptor = dawn::middleware::gameplay::descriptor;
namespace wire = dawn::middleware::social::realtime;

constexpr std::uint64_t kLobby = 0x0184000000000042ULL;
constexpr std::uint64_t kOwner = 0x0110000100000101ULL;
constexpr std::uint64_t kJoiner = 0x0110000100000001ULL;
constexpr std::uint64_t kMachine = 0xA13B47C90D2E5F61ULL;
constexpr std::uint32_t kAddress = 0x7F000001U;
constexpr std::uint16_t kPort = 30976U;

[[nodiscard]] bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

[[nodiscard]] wire::Lobby lobby_fixture() noexcept {
    wire::Lobby lobby{};
    lobby.id = kLobby;
    lobby.ownerPlatformId = kOwner;
    lobby.members[0].platformId = kOwner;
    lobby.members[1].platformId = kJoiner;
    lobby.memberCount = 2;
    return lobby;
}

[[nodiscard]] wire::Event event_fixture() noexcept {
    descriptor::JoinEndpoint join{};
    join.machineId = kMachine;
    join.address = kAddress;
    join.port = kPort;
    join.onlineSessionId = kLobby;
    std::array<std::byte, descriptor::kDescriptorSize> encoded{};
    if (!descriptor::build(join, encoded)) {
        return {};
    }

    wire::Event event{};
    event.sequence = 77;
    event.kind = wire::EventKind::gameplayBootstrapDescriptor;
    event.lobbyId = kLobby;
    event.sourcePlatformId = kOwner;
    event.memberPlatformId = kJoiner;
    event.payloadSize = encoded.size();
    std::copy(encoded.begin(), encoded.end(), event.payload.begin());
    return event;
}

[[nodiscard]] bool rejected(const wire::Lobby& lobby,
                            std::uint64_t localPlatformId,
                            const wire::Event& event,
                            bootstrap::Rejection expected,
                            const char* name) noexcept {
    const bootstrap::Decision decision = bootstrap::validate(lobby, localPlatformId, event);
    return require(!decision.accepted() && decision.rejection == expected, name);
}

} // namespace

int main() {
    bool passed = true;
    const wire::Lobby goodLobby = lobby_fixture();
    const wire::Event goodEvent = event_fixture();

    passed &= require(bootstrap::validate(goodLobby, kJoiner, goodEvent).accepted(),
                      "exact central descriptor accepted for joined recipient");
    passed &= require(std::string_view(bootstrap::rejection_name(bootstrap::Rejection::none))
                          == "none",
                      "accepted rejection name is stable");

    wire::Event event = goodEvent;
    event.kind = wire::EventKind::memberChanged;
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::kind,
                       "non-bootstrap event rejected");

    event = goodEvent;
    event.sequence = 0;
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::sequence,
                       "zero event sequence rejected");

    event = goodEvent;
    event.lobbyId += 1;
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::lobby,
                       "foreign lobby descriptor rejected");

    event = goodEvent;
    event.sourcePlatformId = kJoiner;
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::owner,
                       "non-owner descriptor source rejected");

    event = goodEvent;
    event.memberPlatformId = kOwner;
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::recipient,
                       "descriptor for another recipient rejected");

    wire::Lobby lobby = goodLobby;
    lobby.members[1] = {};
    lobby.memberCount = 1;
    passed &= rejected(lobby,
                       kJoiner,
                       goodEvent,
                       bootstrap::Rejection::membership,
                       "recipient absent from authoritative lobby rejected");

    lobby = goodLobby;
    lobby.members[0] = {};
    passed &= rejected(lobby,
                       kJoiner,
                       goodEvent,
                       bootstrap::Rejection::membership,
                       "descriptor source owner absent from authoritative lobby rejected");

    lobby = goodLobby;
    lobby.members[2].platformId = kJoiner;
    lobby.memberCount = 3;
    passed &= rejected(lobby,
                       kJoiner,
                       goodEvent,
                       bootstrap::Rejection::membership,
                       "duplicate recipient membership rejected");

    lobby = goodLobby;
    lobby.memberCount = lobby.members.size() + 1U;
    passed &= rejected(lobby,
                       kJoiner,
                       goodEvent,
                       bootstrap::Rejection::membership,
                       "out-of-range membership count rejected before iteration");

    event = goodEvent;
    event.payloadSize -= 1U;
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::payload,
                       "short descriptor rejected");

    event = goodEvent;
    std::fill_n(event.payload.begin(), sizeof(std::uint64_t), std::byte{});
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::machine,
                       "zero descriptor machine rejected");

    event = goodEvent;
    std::fill_n(event.payload.begin() + 8, sizeof(std::uint32_t), std::byte{});
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::address,
                       "zero local address rejected");

    event = goodEvent;
    event.payload[12] = std::byte{1};
    event.payload[13] = std::byte{0};
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::port,
                       "odd local port rejected");

    event = goodEvent;
    event.payload[93] = std::byte{6};
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::method,
                       "relay transport method rejected");

    event = goodEvent;
    event.payload[110] ^= std::byte{1};
    passed &= rejected(goodLobby,
                       kJoiner,
                       event,
                       bootstrap::Rejection::session,
                       "descriptor session not matching lobby rejected");

    event = goodEvent;
    event.memberPlatformId = kOwner;
    passed &= require(bootstrap::validate(goodLobby, kOwner, event).accepted(),
                      "same descriptor accepted for central lobby owner recipient");

    if (!passed) {
        return 1;
    }
    std::puts("PASS: central gameplay bootstrap client validation contract");
    return 0;
}
