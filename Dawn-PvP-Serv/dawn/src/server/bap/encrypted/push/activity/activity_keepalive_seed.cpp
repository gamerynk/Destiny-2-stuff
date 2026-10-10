#include <Windows.h>

#include "../../../../../state/account/account_state.h"
#include "../../../../../state/activity/defaults/activity_defaults_snapshot.h"
#include "../../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../../state/runtime/runtime.h"
#include "internal.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace {

// member field 1 is a skip test; -1 is the only value that keeps the member (client drops it when the unsigned read is <= 0x1FF)
constexpr std::int32_t kMemberSkipTest = -1;
// member field 2 rides a 0x80000000 bias and the captured reference sends wire 0x7FFFFFFF, logical -1; seeding zero cost the ship and the banner
constexpr std::int32_t kUnsetOpaque = -1;
constexpr std::uint8_t kDefaultTransitionToken = 1;

[[nodiscard]] bool seed_transition_state(std::uint64_t sessionId,
                                         std::int32_t regionIndex) noexcept {
    state::activity::membership::AuthoritativeUpdate update{};
    // no teleport is published here; a fabricated one is a transition the client never asked for
    // the token fills every member lane of all 64 region records; zero is never sent
    update.hasTransitionToken = true;
    update.transitionToken = kDefaultTransitionToken;
    if (regionIndex >= 0) {
        update.hasRegion = true;
        update.region.index = regionIndex;
    }
    state::activity::membership::PendingMutation mutation{};
    const bool seeded =
        state::activity::membership::prepare_authoritative(sessionId, update, mutation)
        && state::activity::membership::commit(mutation);
    SecureZeroMemory(&mutation, sizeof mutation);
    return seeded;
}

} // namespace

bool seed_identity(std::uint64_t sessionId,
                   std::uint64_t memberKey,
                   std::uint64_t characterSoid) noexcept {
    if (memberKey == 0) {
        return false;
    }
    const state::AccountState account = state::account_snapshot();
    state::activity::membership::Identity identity{};
    identity.memberKey = memberKey;
    identity.smallOpaque = kMemberSkipTest;
    identity.signedOpaque = kUnsetOpaque;
    identity.joinIdentity = memberKey;
    identity.accountSoid = account.primarySoid;
    // this message creates the player, so its character must be the one the client signed in on
    identity.opaqueSoid =
        characterSoid != 0 ? characterSoid : state::account::selected_character_soid(account);
    state::activity::membership::PendingMutation mutation{};
    const bool seeded = state::activity::membership::prepare_identity(sessionId, identity, mutation)
                        && state::activity::membership::commit(mutation);
    SecureZeroMemory(&mutation, sizeof mutation);
    return seeded;
}

bool seed_transition_token(std::uint64_t sessionId) noexcept {
    return seed_transition_state(sessionId, state::activity::membership::kAbsentRegionIndex);
}

bool seed_transition_region(std::uint64_t sessionId, std::int32_t regionIndex) noexcept {
    if (regionIndex < 0) {
        return false;
    }
    return seed_transition_state(sessionId, regionIndex);
}

} // namespace dawn::server::bap::encrypted::push::activity
