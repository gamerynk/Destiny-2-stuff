#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "middleware/gameplay/descriptor/join_descriptor.h"
#include "middleware/social/membership_projection.h"
#include "realtime_store.h"

namespace {

namespace descriptor = dawn::middleware::gameplay::descriptor;
namespace projection = dawn::middleware::social::membership_projection;
namespace wire = dawn::middleware::social::realtime;

constexpr std::uint64_t kOwner = 0x0110000100000101ULL;
constexpr std::uint64_t kOwnerAccount = 0x9EAA000100200100ULL;
constexpr std::uint64_t kJoiner = 0x0110000100000001ULL;
constexpr std::uint64_t kJoinerAccount = 0x9EAA000100100100ULL;

[[nodiscard]] bool friends(std::uint64_t left, std::uint64_t right) noexcept {
    return (left == kOwner && right == kJoiner) || (left == kJoiner && right == kOwner);
}

[[nodiscard]] bool require(bool condition, const char* description) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
    return condition;
}

[[nodiscard]] const wire::Event* find_event(const wire::Response& response,
                                            wire::EventKind kind) noexcept {
    for (std::size_t index = 0; index < response.eventCount; ++index) {
        if (response.events[index].kind == kind) {
            return &response.events[index];
        }
    }
    return nullptr;
}

[[nodiscard]] std::array<std::byte, descriptor::kDescriptorSize>
make_descriptor(std::uint64_t lobbyId, std::uint64_t machineId) noexcept {
    descriptor::JoinEndpoint endpoint{};
    endpoint.machineId = machineId;
    endpoint.address = 0xCB00712AU;
    endpoint.port = 27016U;
    endpoint.onlineSessionId = lobbyId;
    std::array<std::byte, descriptor::kDescriptorSize> output{};
    if (!descriptor::build(endpoint, output)) {
        output = {};
    }
    return output;
}

[[nodiscard]] bool publish_descriptor(
    std::uint64_t platformId,
    std::uint64_t accountSoid,
    std::uint64_t now,
    std::uint64_t lobbyId,
    const std::array<std::byte, descriptor::kDescriptorSize>& value,
    std::uint64_t cursor,
    wire::Response& response) noexcept {
    wire::Request request{};
    request.operation = wire::Operation::publishNativeFireteamDescriptor;
    request.lobbyId = lobbyId;
    request.cursor = cursor;
    request.payloadSize = value.size();
    std::copy(value.begin(), value.end(), request.payload.begin());
    return restoration::realtime_store::execute(
        platformId, accountSoid, now, request, &friends, response);
}

} // namespace

int main() {
    bool passed = true;
    passed &= require(restoration::realtime_store::begin_game_session(
                          kOwner, kOwnerAccount, 1U, 100U),
                      "owner begins an authenticated game generation");
    wire::Request request{};
    wire::Response response{};
    request.operation = wire::Operation::ensureSoloLobby;
    request.valueA = 1;
    request.valueB = 6;
    passed &= require(restoration::realtime_store::execute(
                          kOwner, kOwnerAccount, 101U, request, &friends, response)
                          && response.result == 1 && response.lobby.memberCount == 1U,
                      "owner gets one private solo lobby");
    const std::uint64_t lobbyId = response.lobby.id;
    std::uint64_t ownerCursor = response.cursor;
    passed &= require(lobbyId != 0U
                          && find_event(response,
                                        wire::EventKind::gameplayBootstrapDescriptor)
                                 == nullptr,
                      "owner never receives a descriptor telling it to join itself");

    passed &= require(restoration::realtime_store::begin_game_session(
                          kJoiner, kJoinerAccount, 1U, 101U),
                      "joiner begins an authenticated game generation");
    request = {};
    request.operation = wire::Operation::ensureSoloLobby;
    request.valueA = 1;
    request.valueB = 6;
    passed &= require(restoration::realtime_store::execute(
                          kJoiner, kJoinerAccount, 102U, request, &friends, response)
                          && response.result == 1 && response.lobby.id != lobbyId,
                      "joiner gets a distinct private solo lobby");
    std::uint64_t joinerCursor = response.cursor;
    passed &= require(restoration::realtime_store::mark_gameplay_ready(
                          kOwner, kOwnerAccount, 1U, 103U)
                          && restoration::realtime_store::mark_gameplay_ready(
                              kJoiner, kJoinerAccount, 1U, 103U),
                      "both selected characters become gameplay-ready");

    restoration::realtime_store::Presence ownerPresence{};
    passed &= require(restoration::realtime_store::presence(
                          kOwner, 103U, ownerPresence)
                          && ownerPresence.online && !ownerPresence.joinable,
                      "owner stays unadvertised until its native descriptor exists");
    request = {};
    request.operation = wire::Operation::joinLobby;
    request.lobbyId = lobbyId;
    request.cursor = joinerCursor;
    passed &= require(restoration::realtime_store::execute(
                          kJoiner, kJoinerAccount, 104U, request, &friends, response)
                          && response.result == 1 && response.lobby.memberCount == 2,
                      "native JoinLobby can commit membership before route publication");
    passed &= require(restoration::realtime_store::presence(kOwner,104U,ownerPresence)
                          && !ownerPresence.joinable,
                      "early native membership does not advertise an unpublished gameplay route");
    request.operation = wire::Operation::leaveLobby;
    passed &= require(restoration::realtime_store::execute(
                          kJoiner,kJoinerAccount,104U,request,&friends,response)
                          && response.result == 1 && response.lobby.id == 0,
                      "real leave remains immediate before descriptor publication");
    joinerCursor = response.cursor;

    constexpr std::uint64_t kOwnerMachine = 0xC4E8A6D20F317B95ULL;
    const auto ownerDescriptor = make_descriptor(lobbyId, kOwnerMachine);
    auto wrongSessionDescriptor = ownerDescriptor;
    wrongSessionDescriptor[110U] ^= std::byte{1U};
    passed &= require(publish_descriptor(kOwner,
                                         kOwnerAccount,
                                         105U,
                                         lobbyId,
                                         wrongSessionDescriptor,
                                         ownerCursor,
                                         response)
                          && response.result == 8,
                      "store rejects a descriptor for a different online session");
    ownerCursor = response.cursor;
    passed &= require(publish_descriptor(kJoiner,
                                         kJoinerAccount,
                                         105U,
                                         lobbyId,
                                         ownerDescriptor,
                                         joinerCursor,
                                         response)
                          && response.result == 15,
                      "non-owner cannot publish the owner's route");
    joinerCursor = response.cursor;
    passed &= require(publish_descriptor(kOwner,
                                         kOwnerAccount,
                                         106U,
                                         lobbyId,
                                         ownerDescriptor,
                                         ownerCursor,
                                         response)
                          && response.result == 1
                          && find_event(response,
                                        wire::EventKind::gameplayBootstrapDescriptor)
                                 == nullptr,
                      "owner publishes exact bytes without a self-join event");
    ownerCursor = response.cursor;
    restoration::realtime_store::GameplayBootstrapDescriptor copied{};
    passed &= require(restoration::realtime_store::native_fireteam_descriptor(
                          lobbyId, kOwner, copied)
                          && copied == ownerDescriptor
                          && !restoration::realtime_store::native_fireteam_descriptor(
                              lobbyId, kJoiner, copied),
                      "Family-7 lookup is exact and owner-authorized");
    passed &= require(restoration::realtime_store::presence(
                          kOwner, 106U, ownerPresence)
                          && ownerPresence.joinable,
                      "owner becomes joinable only after publication");

    request = {};
    request.operation = wire::Operation::joinLobby;
    request.lobbyId = lobbyId;
    request.cursor = joinerCursor;
    passed &= require(restoration::realtime_store::execute(
                          kJoiner, kJoinerAccount, 107U, request, &friends, response)
                          && response.result == 1 && response.lobby.memberCount == 2U
                          && response.lobby.ownerPlatformId == kOwner,
                      "requester joins social membership without changing authority");
    passed &= require(find_event(response,
                                 wire::EventKind::gameplayBootstrapDescriptor)
                          == nullptr,
                      "social membership never substitutes a server-owned gameplay endpoint");
    joinerCursor = response.cursor;

    request = {};
    request.operation = wire::Operation::poll;
    request.cursor = ownerCursor;
    passed &= require(restoration::realtime_store::execute(
                          kOwner, kOwnerAccount, 108U, request, &friends, response)
                          && response.result == 1
                          && find_event(response, wire::EventKind::memberChanged) != nullptr
                          && find_event(response,
                                        wire::EventKind::gameplayBootstrapDescriptor)
                                 == nullptr,
                      "owner sees membership but never a reciprocal join descriptor");
    ownerCursor = response.cursor;
    passed &= require(!restoration::realtime_store::queue_owner_gameplay_admission(
                          lobbyId, kJoiner),
                      "retired synthetic owner admission remains fail-closed");

    request = {};
    request.operation = wire::Operation::publishMembershipProjection;
    request.lobbyId = lobbyId;
    request.targetPlatformId = kOwner;
    request.payloadSize = 1U;
    request.payload[0] = std::byte{1U};
    request.cursor = joinerCursor;
    passed &= require(restoration::realtime_store::execute(
                          kJoiner, kJoinerAccount, 109U, request, &friends, response)
                          && response.result == 15,
                      "retired membership projection route is fail-closed");
    joinerCursor = response.cursor;

    std::array<std::byte, projection::kDecodedSize> projectionDecoded{};
    std::array<std::byte, projection::kStateSize> projectionState{};
    (void)projection::write_at(projectionDecoded,
                               projection::kRevisionOffset,
                               std::uint32_t{1U});
    (void)projection::write_at(projectionDecoded,
                               projection::kBaseRevisionOffset,
                               std::int32_t{0});
    (void)projection::write_at(projectionDecoded,
                               projection::kMemberCountOffset,
                               std::uint16_t{2U});
    (void)projection::write_at(projectionDecoded,
                               projection::kPlayerCountOffset,
                               std::uint16_t{2U});
    (void)projection::write_at(projectionDecoded,
                               projection::kHashOffset,
                               std::uint32_t{0x13572468U});
    (void)projection::write_at(projectionState,
                               projection::kStateRevisionOffset,
                               std::uint32_t{1U});
    (void)projection::write_at(projectionState,
                               projection::kStateMemberCountOffset,
                               std::uint32_t{2U});
    (void)projection::write_at(projectionState,
                               projection::kStatePlayerCountOffset,
                               std::uint32_t{2U});
    std::array<std::byte, wire::kEventPayloadCapacity> projectionPayload{};
    std::size_t projectionBytes = 0U;
    passed &= require(projection::compress(projectionDecoded,
                                           projectionState,
                                           projectionPayload,
                                           projectionBytes),
                      "valid two-member projection fits the realtime event payload");

    request = {};
    request.operation = wire::Operation::publishMembershipProjection;
    request.lobbyId = lobbyId;
    request.targetPlatformId = kOwner;
    request.payloadSize = projectionBytes;
    std::copy_n(projectionPayload.begin(), projectionBytes, request.payload.begin());
    request.cursor = joinerCursor;
    passed &= require(restoration::realtime_store::execute(
                          kJoiner, kJoinerAccount, 110U, request, &friends, response)
                          && response.result == 15,
                      "even a well-formed requester projection cannot mutate the native owner");
    joinerCursor = response.cursor;

    request = {};
    request.operation = wire::Operation::poll;
    request.cursor = ownerCursor;
    passed &= require(restoration::realtime_store::execute(
                          kOwner, kOwnerAccount, 111U, request, &friends, response),
                      "owner polls the recipient-specific projection event");
    passed &= require(find_event(response,
                                 wire::EventKind::membershipProjection)
                          == nullptr,
                      "owner receives no synthetic membership mutation after requester join");
    ownerCursor = response.cursor;

    request = {};
    request.operation = wire::Operation::setOwner;
    request.lobbyId = lobbyId;
    request.targetPlatformId = kJoiner;
    request.cursor = ownerCursor;
    passed &= require(restoration::realtime_store::execute(
                          kOwner, kOwnerAccount, 112U, request, &friends, response)
                          && response.result == 1
                          && response.lobby.ownerPlatformId == kJoiner,
                      "explicit ownership transfer preserves lobby membership");
    restoration::realtime_store::Presence newOwnerPresence{};
    passed &= require(restoration::realtime_store::presence(
                          kJoiner, 112U, newOwnerPresence)
                          && !newOwnerPresence.joinable
                          && !restoration::realtime_store::native_fireteam_descriptor(
                              lobbyId, kOwner, copied),
                      "ownership transfer invalidates the previous owner's native route");

    constexpr std::uint64_t kJoinerMachine = 0xD5F9B7E310428CA6ULL;
    const auto joinerDescriptor = make_descriptor(lobbyId, kJoinerMachine);
    passed &= require(publish_descriptor(kJoiner,
                                         kJoinerAccount,
                                         113U,
                                         lobbyId,
                                         joinerDescriptor,
                                         joinerCursor,
                                         response)
                          && response.result == 1
                          && restoration::realtime_store::native_fireteam_descriptor(
                              lobbyId, kJoiner, copied)
                          && copied == joinerDescriptor,
                      "new owner must publish its own exact retail descriptor");

    restoration::realtime_store::Presence expiredOwnerPresence{};
    wire::Lobby retainedLobby{};
    passed &= require(
        !restoration::realtime_store::presence(kJoiner, 150U, expiredOwnerPresence)
            && !expiredOwnerPresence.online
            && restoration::realtime_store::lobby_snapshot(lobbyId, retainedLobby)
            && retainedLobby.memberCount == 2U
            && retainedLobby.ownerPlatformId == kJoiner
            && std::any_of(retainedLobby.members.begin(),
                           retainedLobby.members.begin() + retainedLobby.memberCount,
                           [](const wire::Member& member) noexcept {
                               return member.platformId == kOwner;
                           })
            && std::any_of(retainedLobby.members.begin(),
                           retainedLobby.members.begin() + retainedLobby.memberCount,
                           [](const wire::Member& member) noexcept {
                               return member.platformId == kJoiner;
                           }),
        "expired heartbeat hides online presence without mutating shared lobby authority");

    request = {};
    request.operation = wire::Operation::poll;
    request.cursor = joinerCursor;
    passed &= require(
        restoration::realtime_store::execute(
            kJoiner, kJoinerAccount, 151U, request, &friends, response)
            && response.result == 1 && response.lobby.id == lobbyId
            && response.lobby.memberCount == 2U && response.lobby.ownerPlatformId == kJoiner
            && restoration::realtime_store::presence(
                kJoiner, 151U, expiredOwnerPresence)
            && expiredOwnerPresence.online,
        "resumed heartbeat adopts the unchanged shared lobby without a synthetic leave");

    request = {};
    request.operation = wire::Operation::leaveLobby;
    request.lobbyId = lobbyId;
    request.cursor = response.cursor;
    passed &= require(
        restoration::realtime_store::execute(
            kJoiner, kJoinerAccount, 152U, request, &friends, response)
            && response.result == 1
            && restoration::realtime_store::lobby_snapshot(lobbyId, retainedLobby)
            && retainedLobby.memberCount == 1U
            && retainedLobby.ownerPlatformId == kOwner
            && retainedLobby.members[0].platformId == kOwner,
        "explicit LeaveLobby still removes a retained member and migrates authority");

    if (!passed) {
        return 1;
    }
    std::puts("PASS: owner-native descriptor, shared-lobby lease, and no synthetic projection");
    return 0;
}
