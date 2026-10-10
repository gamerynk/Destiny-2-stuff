#pragma once

#include "../../../internal.h"

namespace dawn::server::bap::encrypted::push::activity::spawn_sync {

[[nodiscard]] inline bool join_contract_ready(
    const Session& session, const state::activity::SessionRecord& record) noexcept {
    const auto& c = session.activityContract;
    const auto& m = record.membership;
    return session.authenticated && !session.activityJoinedForeignSession
           && session.credentials.platformId != 0 && session.activitySessionId != 0
           && session.activityMemberKey != 0 && session.activityCharacterSoid != 0
           && c.allocated && c.joined && c.identity && c.patchEpoch
           && c.authoritative && c.region && c.reportedRegion >= 0
           && c.transitionToken && c.reportedTransitionToken != 0
           && c.refresh && c.acknowledgement
           && record.occupied && record.joined && !record.restoredDormant
           && !record.publicWorld && record.sessionId == session.activitySessionId
           && record.memberKey == session.activityMemberKey
           && m.hasIdentity && m.identity.memberKey == session.activityMemberKey
           && m.identity.opaqueSoid == session.activityCharacterSoid
           && m.revision != 0 && m.acknowledgedRevision == m.revision
           && m.region.index == c.reportedRegion && m.hasTransitionToken
           && m.transitionToken == c.reportedTransitionToken;
}

} // namespace dawn::server::bap::encrypted::push::activity::spawn_sync
