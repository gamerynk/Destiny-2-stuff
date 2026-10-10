#pragma once

#include <array>
#include <limits>
#include "../queuez_state_validation.h"

namespace dawn::server::bap::encrypted::queuez {

[[nodiscard]] inline bool prepare_family3_subscription_progress(
    const SessionState& before, const middleware::queuez::Subscription& subscription,
    std::span<std::byte> output, std::size_t& written, SessionState& after) noexcept {
    after = before;
    written = 0;
    if (!valid(before) || subscription.familyType != kRosterFamilyType
        || !before.family3Active || !before.family4Active
        || before.family3Phase != Family3Phase::responseOnly
        || before.family3RootSoid != subscription.familyRootSoid
        || before.family3Version == (std::numeric_limits<std::int32_t>::max)()) {
        return false;
    }
    const std::array families{middleware::queuez::Family{
        kRosterFamilyType, before.family3RootSoid, before.family3Version + 1, 0, {}}};
    if (!middleware::queuez::encode_update(families, output, written)) return false;
    after.family3Version = before.family3Version + 1;
    return true;
}

} // namespace dawn::server::bap::encrypted::queuez
