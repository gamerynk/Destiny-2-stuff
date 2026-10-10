#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "steam/runtime/social/owner_gameplay_admission.h"

namespace {

namespace admission = dawn::steam::runtime::social::owner_gameplay_admission;
namespace wire = dawn::middleware::social::realtime;

constexpr std::uint64_t kLobby = 0x0184000000000042ULL;
constexpr std::uint64_t kOwner = 0x0110000100000101ULL;
constexpr std::uint64_t kJoiner = 0x0110000100000001ULL;

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
    wire::Event event{};
    event.sequence = 77;
    event.kind = wire::EventKind::ownerGameplayAdmission;
    event.lobbyId = kLobby;
    event.sourcePlatformId = kJoiner;
    event.memberPlatformId = kOwner;
    event.payloadSize = dawn::middleware::gameplay::descriptor::kDescriptorSize;
    event.payload[0] = std::byte{1};
    event.payload[8] = std::byte{127};
    event.payload[9] = std::byte{0};
    event.payload[10] = std::byte{0};
    event.payload[11] = std::byte{1};
    event.payload[12] = std::byte{0x00};
    event.payload[13] = std::byte{0x79};
    std::copy_n(event.payload.begin() + 8, 4, event.payload.begin() + 38);
    std::copy_n(event.payload.begin() + 12, 2, event.payload.begin() + 42);
    for (std::size_t index = 0; index < sizeof(kLobby); ++index) {
        event.payload[110 + index] =
            static_cast<std::byte>((kLobby >> (index * 8U)) & 0xFFU);
    }
    return event;
}

} // namespace

int main() {
    bool passed = true;
    const wire::Lobby goodLobby = lobby_fixture();
    const wire::Event goodEvent = event_fixture();

    auto decision = admission::validate(goodLobby, kOwner, goodEvent, 0);
    passed &= require(decision.accepted()
                          && decision.descriptorSize
                                 == dawn::middleware::gameplay::descriptor::kDescriptorSize,
                      "exact owner admission accepted");

    wire::Event event = goodEvent;
    event.kind = wire::EventKind::lobbyInvite;
    passed &= require(!admission::validate(goodLobby, kOwner, event, 0).accepted(),
                      "ordinary lobby invite rejected");
    passed &= require(!admission::validate(goodLobby, kOwner, goodEvent, 77).accepted(),
                      "duplicate sequence rejected");
    passed &= require(!admission::validate(goodLobby, kJoiner, goodEvent, 0).accepted(),
                      "non-owner local client rejected");

    event = goodEvent;
    event.memberPlatformId = kJoiner;
    passed &= require(!admission::validate(goodLobby, kOwner, event, 0).accepted(),
                      "wrong event recipient rejected");

    wire::Lobby lobby = goodLobby;
    lobby.members[1] = {};
    lobby.memberCount = 1;
    passed &= require(!admission::validate(lobby, kOwner, goodEvent, 0).accepted(),
                      "absent source member rejected");

    event = goodEvent;
    event.payloadSize -= 1U;
    passed &= require(!admission::validate(goodLobby, kOwner, event, 0).accepted(),
                      "short descriptor rejected");

    event = goodEvent;
    event.payload[0] = std::byte{};
    passed &= require(!admission::validate(goodLobby, kOwner, event, 0).accepted(),
                      "zero descriptor machine rejected");

    event = goodEvent;
    event.payload[8] = std::byte{};
    event.payload[9] = std::byte{};
    event.payload[10] = std::byte{};
    event.payload[11] = std::byte{};
    passed &= require(!admission::validate(goodLobby, kOwner, event, 0).accepted(),
                      "zero descriptor address rejected");

    event = goodEvent;
    event.payload[110] ^= std::byte{1};
    passed &= require(!admission::validate(goodLobby, kOwner, event, 0).accepted(),
                      "descriptor session mismatch rejected");

    if (passed) {
        std::puts("PASS: owner gameplay admission contract");
        return 0;
    }
    return 1;
}
