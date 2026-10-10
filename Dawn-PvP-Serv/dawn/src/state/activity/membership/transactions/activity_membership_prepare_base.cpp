#include "../../transactions/internal.h"
#include "internal.h"

namespace dawn::state::activity::membership::transactions {

const SessionRecord* prepare_base(const ActivityState& state,
                                  std::uint64_t primarySoid,
                                  std::uint64_t sessionId,
                                  PendingMutation& mutation) noexcept {
    const std::size_t target = activity::transactions::find_session(state, sessionId);
    if (target == kInvalidSessionSlot) {
        return nullptr;
    }
    const SessionRecord& record = state.sessions[target];
    if (!record.joined || record.joinedRevision == kInvalidRevision
        || record.recordRevision == kInvalidRevision || record.ownerAccountSoid != primarySoid) {
        return nullptr;
    }
    mutation.sessionId = sessionId;
    mutation.expectedStateRevision = state.stateRevision;
    mutation.expectedRecordRevision = record.recordRevision;
    mutation.expectedPrimarySoid = primarySoid;
    mutation.targetSlot = target;
    mutation.prepared = true;
    return &record;
}

void publish_change(ActivityState& state, SessionRecord& record) noexcept {
    ++state.stateRevision;
    record.recordRevision = state.stateRevision;
}

} // namespace dawn::state::activity::membership::transactions
