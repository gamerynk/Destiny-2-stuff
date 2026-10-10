#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "middleware/bap/activity_message/replicate_membership.h"

namespace membership =
    dawn::middleware::bap::activity_message::replicate_membership;

namespace {

[[nodiscard]] bool require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

membership::MembershipSnapshot snapshot(bool citizen) {
    membership::MembershipSnapshot value{};
    value.identity.memberKey = 0x1122334455667788ULL;
    value.identity.field1 = -1;
    value.identity.field2 = -1;
    value.identity.field3 = 0x8877665544332211ULL;
    value.identity.accountSoid = 0x9EAA000100100100ULL;
    value.identity.field5 = 0x9EAA000100100101ULL;
    value.identity.field6 = 0x1020304050607080ULL;
    value.teleport.sliceSetIndex = -1;
    value.revision = 1;
    value.epoch = 1;
    value.transitionToken = 1;
    if (citizen) {
        value.citizen.present = true;
        value.citizen.memberKey = 0xAABBCCDDEEFF0011ULL;
        value.citizen.onlineSessionId = 0x9EAA000100200002ULL;
        value.citizen.regionIndex = 408;
        value.citizen.ambassadorSlot = 1;
        for (std::size_t index = 0; index < value.citizen.descriptor.size(); ++index) {
            value.citizen.descriptor[index] = static_cast<std::byte>(index);
        }
    }
    return value;
}

} // namespace

int main() {
    static_assert(membership::kEncodedSize == 3'748);
    static_assert(membership::kCitizenEncodedSize == 3'876);
    static_assert(membership::region_block_start_bit(membership::MembershipSnapshot{})
                  == membership::kRegionBlockStartBit);

    std::array<std::byte, membership::kCitizenEncodedSize> output{};
    std::size_t written = 0;
    auto local = snapshot(false);
    bool ok = require(membership::encode_replicate_membership(local, output, written),
                      "local membership encodes")
              && require(written == membership::kEncodedSize,
                         "complete local membership is 3,748 bytes");

    written = 0;
    auto publicCitizen = snapshot(true);
    ok = require(membership::encode_replicate_membership(publicCitizen, output, written),
                 "Citizen membership encodes")
         && require(written == membership::kCitizenEncodedSize,
                    "complete Citizen membership is 3,876 bytes")
         && require(membership::region_block_start_bit(publicCitizen)
                        == membership::kRegionBlockStartBit,
                    "Citizen descriptor does not shift the member-table boundary")
         && ok;

    if (!ok) {
        return 1;
    }
    std::puts("PASS: complete PC 1.59 Citizen membership is 3,876 bytes with descriptor-only region growth");
    return 0;
}
