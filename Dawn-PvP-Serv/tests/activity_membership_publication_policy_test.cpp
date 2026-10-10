#include <cstdio>
#include <initializer_list>

#include "server/bap/encrypted/push/activity/activity_membership_publication_policy.h"

namespace policy = dawn::server::bap::encrypted::push::activity;

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
    using Phase = policy::MembershipPublicationPhase;
    bool ok = true;
    ok = require(policy::membership_publication_phase(false, 408, -1, -1) == Phase::none,
                 "no snapshot publishes nothing")
         && ok;
    ok = require(policy::membership_publication_phase(true, 408, -1, -1) == Phase::baseline,
                 "first region publisher turn is the unspent baseline")
         && ok;
    ok = require(!policy::membership_phase_sends_snapshot(Phase::baseline),
                 "baseline must not spend the membership revision")
         && ok;
    ok = require(policy::membership_publication_phase(true, 408, 408, -1)
                     == Phase::advertisement,
                 "descriptor follows the delivered baseline")
         && ok;
    ok = require(policy::membership_phase_sends_snapshot(Phase::advertisement),
                 "advertisement spends the revision with its descriptor")
         && ok;
    ok = require(policy::membership_publication_phase(true, 408, 408, 408) == Phase::steady,
                 "region settles only after both phases")
         && ok;
    ok = require(policy::membership_publication_phase(true, 24, 408, 408) == Phase::baseline,
                 "same-planet region move starts a fresh baseline")
         && ok;
    ok = require(policy::membership_publication_phase(true, 24, 24, 408)
                     == Phase::advertisement,
                 "same-planet region descriptor cannot skip its baseline")
         && ok;
    ok = require(policy::membership_publication_phase(true, -1, -1, -1) == Phase::steady,
                 "regionless membership has no Citizen ordering")
         && ok;
    ok = require(policy::membership_phase_sends_snapshot(Phase::steady),
                 "ordinary settled membership still publishes")
         && ok;

    const auto publicSends = [](bool ready, bool snapshot, Phase phase, bool published, bool ack) {
#ifdef PUBLIC_FIRST_ONLY_BASELINE
        (void)ack;
        return ready && snapshot && policy::membership_phase_sends_snapshot(phase)
            && (phase == Phase::advertisement || !published);
#else
        return policy::public_membership_should_publish(ready,snapshot,phase,published,ack);
#endif
    };
    ok = require(publicSends(true,true,Phase::steady,true,false),
                 "late PUBLIC participant revision must publish after the first snapshot") && ok;
    ok = require(!publicSends(true,true,Phase::steady,true,true),
                 "acknowledged unchanged PUBLIC revision stays quiet") && ok;
    for (const bool published : {false,true}) {
        for (const bool ack : {false,true}) {
            for (const auto phase : {Phase::none,Phase::baseline,Phase::advertisement,Phase::steady}) {
                ok = require(!publicSends(false,true,phase,published,ack),
                             "unsettled PUBLIC setup still blocks membership") && ok;
                ok = require(!publicSends(true,false,phase,published,ack),
                             "missing PUBLIC snapshot still blocks membership") && ok;
            }
            ok = require(!publicSends(true,true,Phase::baseline,published,ack),
                         "unacknowledged revision cannot bypass region baseline") && ok;
            ok = require(publicSends(true,true,Phase::advertisement,published,ack),
                         "region advertisement still publishes") && ok;
        }
    }
    bool acknowledged = true;
    for (unsigned event=0;event<3;++event) {
        acknowledged = false;
        ok = require(publicSends(true,true,Phase::steady,true,acknowledged),
                     "shared join/readiness/leave revision must be delivered") && ok;
        ok = require(publicSends(true,true,Phase::steady,true,acknowledged),
                     "failed/unacknowledged publication remains retryable") && ok;
        acknowledged = true;
        ok = require(!publicSends(true,true,Phase::steady,true,acknowledged),
                     "ack stops retry until another actual State change") && ok;
    }
    ok = require(policy::primary_region_publication_complete(true,true,false,true,true),
                 "social primary completes its delivered descriptor-free region") && ok;
    ok = require(!policy::primary_region_publication_complete(false,true,false,true,true)
                 && !policy::primary_region_publication_complete(true,false,false,true,true),
                 "failed delivery or baseline cannot complete social publication") && ok;
    ok = require(!policy::primary_region_publication_complete(true,true,false,false,true)
                 && !policy::primary_region_publication_complete(true,true,false,true,false),
                 "missing patrol or pending advertisement stays retryable") && ok;
    ok = require(policy::primary_region_publication_complete(true,true,true,false,false),
                 "ready patrol advertisement remains unchanged") && ok;
    if (!ok) {
        return 1;
    }
    std::puts("PASS: Activity membership leaves the revision unspent until Citizen advertisement");
    return 0;
}
