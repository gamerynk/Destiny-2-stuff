#pragma once

#include <cstdint>

#include "../../../../middleware/bap/activity_message/activity_patch_epoch_parser.h"
#include "../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../state/activity/runtime.h"

namespace dawn::server::bap::encrypted::activity_message {

enum class Delivery : std::uint8_t {
    none,
    joinNotifications,
    entitySlotNotification,
    membershipNotification,
    refreshNotifications,
    authoritativeNotifications,
};

enum class MutationDomain : std::uint8_t {
    none,
    entitySlots,
    membership,
    patchEpoch,
};

struct ActivityPlan final {
    std::uint32_t requestMessageType{};
    std::uint32_t correlation{};
    std::uint64_t sessionId{};
    state::activity::entity_slots::PendingMutation entitySlotMutation{};
    state::activity::membership::PendingMutation membershipMutation{};
    middleware::bap::activity_message::patch_epoch::PatchEpoch patchEpoch{};
    std::uint64_t joinCharacterSoid{};
    // the new region has no bubble authority until the roster grants it, so waiting for the next tick leaves it empty
    bool regionMoved{};
    // the transition token moving is the only load-start signal on the wire; the roster's faster cadence runs off it
    bool transitionStarted{};
    Delivery delivery{};
    MutationDomain mutationDomain{};
};

} // namespace dawn::server::bap::encrypted::activity_message
