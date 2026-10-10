#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

#include "middleware/social/realtime_codec.h"
#include "steam/runtime/social/initial_lobby_bootstrap.h"

namespace {

namespace policy = dawn::steam::runtime::social::initial_lobby_bootstrap;
namespace realtime = dawn::middleware::social::realtime;

[[nodiscard]] bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

} // namespace

int main() {
    bool passed = true;
    passed &= require(
        policy::decide({true, true, 0x0110000100000101ULL, 0})
            == policy::Action::ensureSoloLobby,
        "successful authenticated poll pre-creates the missing solo lobby");
    passed &= require(policy::kLobbyType == 1, "bootstrap uses the established lobby type");
    passed &= require(
        policy::decide({true, true, 0x0110000100000101ULL, 0x0184000000000001ULL})
            == policy::Action::none,
        "existing lobby is never replaced by background bootstrap");
    passed &= require(
        policy::decide({true, false, 0x0110000100000101ULL, 0})
            == policy::Action::none,
        "failed poll cannot create a lobby");
    passed &= require(policy::decide({true, true, 0, 0}) == policy::Action::none,
                      "uncommitted local identity cannot create a lobby");
    passed &= require(
        policy::decide({false, true, 0x0110000100000101ULL, 0})
            == policy::Action::none,
        "non-dedicated mode is unchanged");

    realtime::Request ensureRequest{};
    ensureRequest.operation = realtime::Operation::ensureSoloLobby;
    ensureRequest.valueA = policy::kLobbyType;
    ensureRequest.valueB = static_cast<std::int32_t>(realtime::kMemberCapacity);
    std::array<std::byte, realtime::kMaximumRequestSize> encoded{};
    std::size_t encodedSize = 0;
    realtime::Request decoded{};
    passed &= require(
        realtime::encode_request(ensureRequest, encoded, encodedSize),
        "atomic solo-lobby request encodes on the client wire");
    passed &= require(
        realtime::decode_request(std::span(encoded).first(encodedSize), decoded),
        "atomic solo-lobby request is accepted by the server wire decoder");
    passed &= require(
        decoded.operation == realtime::Operation::ensureSoloLobby
            && decoded.valueA == policy::kLobbyType
            && decoded.valueB == static_cast<std::int32_t>(realtime::kMemberCapacity),
        "atomic solo-lobby wire round trip preserves its operation and policy values");

    realtime::Request projectionRequest{};
    projectionRequest.operation = realtime::Operation::publishMembershipProjection;
    encodedSize = 0;
    decoded = {};
    passed &= require(
        realtime::encode_request(projectionRequest, encoded, encodedSize)
            && realtime::decode_request(std::span(encoded).first(encodedSize), decoded)
            && decoded.operation == realtime::Operation::publishMembershipProjection,
        "membership projection operation round trips at the declared protocol boundary");

    realtime::Request descriptorRequest{};
    descriptorRequest.operation = realtime::Operation::publishNativeFireteamDescriptor;
    encodedSize = 0;
    decoded = {};
    passed &= require(
        realtime::encode_request(descriptorRequest, encoded, encodedSize)
            && realtime::decode_request(std::span(encoded).first(encodedSize), decoded)
            && decoded.operation == realtime::Operation::publishNativeFireteamDescriptor,
        "owner-native descriptor operation round trips at the declared protocol boundary");

    realtime::Request unknownRequest{};
    unknownRequest.operation = static_cast<realtime::Operation>(21);
    encodedSize = 0;
    decoded = {};
    passed &= require(
        realtime::encode_request(unknownRequest, encoded, encodedSize),
        "unknown operation can be represented for decoder-boundary testing");
    passed &= require(
        !realtime::decode_request(std::span(encoded).first(encodedSize), decoded),
        "server wire decoder still rejects operations above the declared protocol");
    if (!passed) {
        return 1;
    }
    std::puts("PASS: initial central lobby bootstrap ordering policy");
    return 0;
}
