#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "middleware/datagen/definitions.h"
#include "middleware/datagen/family7/family7_encoder.h"
#include "middleware/gameplay/descriptor/join_descriptor.h"

namespace {

template <typename Value>
Value read(const std::byte* source) noexcept {
    Value value{};
    std::memcpy(&value, source, sizeof value);
    return value;
}

bool require(bool condition, const char* description) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
    return condition;
}

} // namespace

int main() {
    namespace datagen = dawn::middleware::datagen;
    namespace descriptor = dawn::middleware::gameplay::descriptor;
    namespace family7 = datagen::family7;

    constexpr std::uint64_t kAccountSoid = 0x0110000100000101ULL;
    constexpr std::uint64_t kMachineId = 0xC4E8A6D20F317B95ULL;
    constexpr std::uint32_t kAddress = 0xCB00712AU;
    constexpr std::uint16_t kPort = 27016;
    constexpr std::uint64_t kLobbyId = 0x0109000000000008ULL;

    descriptor::JoinEndpoint endpoint{};
    endpoint.machineId = kMachineId;
    endpoint.address = kAddress;
    endpoint.port = kPort;
    endpoint.onlineSessionId = kLobbyId;
    std::array<std::byte, descriptor::kDescriptorSize> joinDescriptor{};
    std::array<std::byte, family7::kAnchorSize> anchor{};
    std::array<std::byte, family7::kBodySize> body{};

    bool passed = true;
    passed &= require(family7::kCompatibilitySignature == 0x5C01U,
                      "Family-7 PC 1.59 compatibility signature is exact");
    passed &= require(descriptor::build(endpoint, joinDescriptor),
                      "central-server join descriptor builds");
    passed &= require(family7::encode_anchor(kAccountSoid, kAccountSoid, anchor),
                      "Family-7 anchor encodes");
    passed &= require(family7::encode_body(kAccountSoid,
                                           joinDescriptor,
                                           family7::kCompatibilitySignature,
                                           body),
                      "Family-7 body encodes");

    std::uint32_t objectId = 0;
    passed &= require(datagen::object_id(datagen::kJoinFamily,
                                         datagen::kJoinAnchorSlot,
                                         objectId)
                          && objectId == datagen::kJoinAnchorObjectId,
                      "Family-7 slot-zero native object id");
    passed &= require(datagen::object_id(datagen::kJoinFamily,
                                         datagen::kJoinDescriptorSlot,
                                         objectId)
                          && objectId == datagen::kJoinDescriptorObjectId,
                      "Family-7 slot-one native object id");

    passed &= require(read<std::uint64_t>(anchor.data()) == kAccountSoid
                          && read<std::uint64_t>(anchor.data() + 8) == kAccountSoid,
                      "Family-7 account-keyed anchor graph");
    passed &= require(read<std::uint64_t>(body.data()) == kAccountSoid,
                      "Family-7 body key");
    passed &= require(read<std::uint32_t>(body.data() + 0x08) == 0x80U,
                      "Family-7 exact descriptor length");
    passed &= require(std::memcmp(body.data() + 0x0C,
                                  joinDescriptor.data(),
                                  joinDescriptor.size()) == 0,
                      "Family-7 descriptor payload is byte exact");
    passed &= require(read<std::uint32_t>(body.data() + 0x8C) == 0U,
                      "Family-7 alignment padding remains zero");
    passed &= require(read<std::uint64_t>(body.data() + 0x90)
                          == family7::kCompatibilitySignature,
                      "Family-7 1.59 compatibility signature");

    passed &= require(read<std::uint64_t>(joinDescriptor.data()) == kMachineId,
                      "descriptor machine identity");
    passed &= require(joinDescriptor[0x08] == std::byte{0xCB}
                          && joinDescriptor[0x09] == std::byte{0x00}
                          && joinDescriptor[0x0A] == std::byte{0x71}
                          && joinDescriptor[0x0B] == std::byte{0x2A},
                      "descriptor local endpoint address");
    passed &= require(joinDescriptor[0x0C] == std::byte{0x88}
                          && joinDescriptor[0x0D] == std::byte{0x69},
                      "descriptor local endpoint port");
    passed &= require(joinDescriptor[0x26] == std::byte{0xCB}
                          && joinDescriptor[0x27] == std::byte{0x00}
                          && joinDescriptor[0x28] == std::byte{0x71}
                          && joinDescriptor[0x29] == std::byte{0x2A},
                      "descriptor public endpoint address");
    passed &= require(joinDescriptor[0x30] == std::byte{1},
                      "descriptor open NAT marker");
    passed &= require(joinDescriptor[0x5D] == std::byte{0},
                      "descriptor centralized direct method");
    passed &= require(read<std::uint64_t>(joinDescriptor.data() + 0x6E) == kLobbyId,
                      "descriptor Steam lobby tail");

    if (!passed) {
        return 1;
    }
    std::puts("PASS: QueueZ Family-7 centralized join wire contract");
    return 0;
}
