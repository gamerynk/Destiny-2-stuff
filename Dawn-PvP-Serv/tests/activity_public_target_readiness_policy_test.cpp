#include <cstdio>

#include "server/bap/internal.h"
#include "server/bap/encrypted/push/activity/activity_membership_publication_policy.h"

namespace {

[[nodiscard]] bool require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

} // namespace

int main() {
    using dawn::server::bap::encrypted::push::activity::
        kPublicMembershipSettleDelayMs;
    using dawn::server::bap::encrypted::push::activity::
        public_membership_settle_deadline;
    using dawn::server::bap::encrypted::push::activity::
        public_membership_settled;

    dawn::server::bap::ActivityClientContract contract{};
    contract.allocated = true;
    contract.joined = true;
    contract.patchEpoch = true;
    contract.refresh = true;

    bool ok = require(contract.ready_for_public_target_roster(true),
                      "assigned activity-host region completes the public setup contract");

    constexpr std::uint64_t readyAtTick = 10'000;
    constexpr std::uint64_t notBeforeTick =
        public_membership_settle_deadline(readyAtTick);
    ok = require(notBeforeTick == readyAtTick + kPublicMembershipSettleDelayMs,
                 "public membership deadline uses the declared settlement window")
         && require(!public_membership_settled(notBeforeTick, 0),
                    "an unarmed deadline never releases membership")
         && require(!public_membership_settled(notBeforeTick - 1, notBeforeTick),
                    "membership stays held immediately before the deadline")
         && require(public_membership_settled(notBeforeTick, notBeforeTick),
                    "membership releases exactly at the deadline")
         && require(public_membership_settled(notBeforeTick + 1, notBeforeTick),
                    "membership remains released after settlement")
         && ok;

    contract.refresh = false;
    ok = require(!contract.ready_for_public_target_roster(true),
                 "public setup still requires the client's refresh")
         && ok;

    if (!ok) {
        return 1;
    }
    std::puts("PASS: public membership uses a latched post-setup settlement deadline");
    return 0;
}
