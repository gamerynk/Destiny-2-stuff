#include "service_outcome_commit.h"

#include "../../../../core/logging/log.h"
#include "../../../../state/activity/bubble_authority/runtime.h"
#include "../../../../state/activity/runtime.h"
#include "../../../../state/matchmaking/matchmaking_state.h"
#include "../../../../state/runtime/runtime.h"
#include "../../../../state/vendors/answered_interactions.h"
#include "../internal.h"

namespace dawn::server::bap::encrypted::transactions {

bool commit(ServiceOutcome& outcome, Publication& publication) noexcept {
    publication = {};
    if (auto* allocation = transaction_if<state::activity::PendingAllocation>(outcome)) {
        const std::uint64_t sessionId = allocation->socialSessionId!=0
            ? allocation->socialSessionId : allocation->sessionId;
        if (sessionId == state::activity::kAbsentSessionId
            || !state::activity::commit(*allocation)) {
            return false;
        }
        publication.activitySessionId = sessionId;
        publication.hasActivitySessionBinding = true;
        return true;
    }
    if (auto* plan = transaction_if<activity_message::ActivityPlan>(outcome)) {
        if (plan->mutationDomain == activity_message::MutationDomain::entitySlots) {
            if (!state::activity::entity_slots::commit(plan->entitySlotMutation)) {
                return false;
            }
            // keepalive only finds a link bound to a session; a link with its own session rebinds to itself
            if (plan->delivery == activity_message::Delivery::joinNotifications
                && plan->sessionId != state::activity::kAbsentSessionId) {
                publication.activitySessionId = plan->sessionId;
                publication.hasActivitySessionBinding = true;
                publication.activitySessionFromJoin = true;
                // a join resets the client's roster container and grant mirror; a kept grant would refuse its placed objects
                state::activity::bubble_authority::clear_grants(plan->sessionId);
            }
            return true;
        }
        if (plan->mutationDomain == activity_message::MutationDomain::membership) {
            return state::activity::membership::commit(plan->membershipMutation);
        }
        return plan->mutationDomain == activity_message::MutationDomain::patchEpoch;
    }
    if (auto* mutation = transaction_if<state::matchmaking::PendingMutation>(outcome)) {
        return state::matchmaking::commit(*mutation);
    }
    if (auto* transaction = transaction_if<EquipmentSwapTransaction>(outcome)) {
        const bool committed = state::commit_equipment_swap(transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=equip stage=transaction_commit result=ok"
                                   : "ev=equip stage=transaction_commit result=fail");
        return committed;
    }
    if (auto* transaction = transaction_if<ItemAcquisitionTransaction>(outcome)) {
        const bool committed = state::commit_item_acquisition(transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=acquire stage=transaction_commit result=ok"
                                   : "ev=acquire stage=transaction_commit result=fail");
        if (committed && transaction->answeredVendor != state::vendors::kAbsentIndex) {
            (void)state::vendors::answer_shown(transaction->answeredVendor);
        }
        return committed;
    }
    if (auto* transaction = transaction_if<SocketPlugTransaction>(outcome)) {
        const bool committed = state::commit_socket_plug(transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=socket_plug stage=transaction_commit result=ok"
                                   : "ev=socket_plug stage=transaction_commit result=fail");
        return committed;
    }
    if (auto* transaction = transaction_if<ItemStateTransaction>(outcome)) {
        const bool committed = state::commit_item_state(transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=item_state stage=transaction_commit result=ok"
                                   : "ev=item_state stage=transaction_commit result=fail");
        return committed;
    }
    if (auto* transaction = transaction_if<ProfileItemAcquisitionTransaction>(outcome)) {
        const bool committed = state::commit_profile_item_acquisition(transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=profile_acquire stage=transaction_commit result=ok"
                                   : "ev=profile_acquire stage=transaction_commit result=fail");
        if (committed && transaction->answeredVendor != state::vendors::kAbsentIndex) {
            (void)state::vendors::answer_shown(transaction->answeredVendor);
        }
        return committed;
    }
    if (auto* transaction = transaction_if<ItemDismantleTransaction>(outcome)) {
        const bool committed = state::commit_item_dismantle(transaction->pending);
        core::log::write(core::log::Channel::server,
                         committed ? core::log::Level::debug : core::log::Level::warn,
                         committed ? "ev=dismantle stage=transaction_commit result=ok"
                                   : "ev=dismantle stage=transaction_commit result=fail");
        return committed;
    }
    if (auto* transaction = transaction_if<VaultTransferTransaction>(outcome)) {
        return state::commit_vault_transfer(transaction->pending);
    }
    if (auto* transaction = transaction_if<AbilitySelectionTransaction>(outcome)) {
        return state::commit_ability_selection(transaction->pending);
    }
    return true;
}

} // namespace dawn::server::bap::encrypted::transactions
