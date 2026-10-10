#include "internal.h"

namespace dawn::state::activity::membership::transactions {

bool commit_authoritative(ActivityState& state,
                          SessionRecord& record,
                          const PendingMutation& prepared) noexcept {
    if (!equal(prepared.authoritativeInput, prepared.authoritativeGuard)) {
        return false;
    }

    // the merge decides the outcome, not a comparison with the prepared plan; refusing on that
    // difference dropped the whole delta, so no revision advanced and the region never moved
    MembershipState merged = merge(record.membership, prepared.authoritativeInput);
    const bool changed = !equal_authoritative(record.membership, merged);
    const bool movesRegion = moves_region(record.membership, merged);
    const bool publishes = changed || movesRegion;
    const bool revisionExhausted = state.stateRevision == activity::kMaximumRevision
                                   || (record.membership.hasIdentity
                                       && record.membership.revision == kMaximumMembershipRevision);
    if (publishes && revisionExhausted) {
        return false;
    }
    if (!publishes) {
        return true;
    }

    if (record.membership.hasIdentity) {
        ++merged.revision;
        merged.acknowledgedRevision = kAbsentRevision;
    }
    record.membership = merged;
    publish_change(state, record);
    return true;
}

} // namespace dawn::state::activity::membership::transactions
