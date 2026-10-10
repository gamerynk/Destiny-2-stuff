#pragma once

#include <cstdint>
#include <string_view>

#include "../../../../../state/activity/defaults/definition.h"
#include "../../../../../state/activity/destination/definition.h"
#include "../../../../../state/activity/membership/activity_membership_query.h"
#include "../../../../../state/build_data/scenarios/definition.h"

namespace dawn::server::bap::encrypted::push::activity {

[[nodiscard]] std::uint16_t
arrival_slice_set(const state::activity::defaults::DefaultDestination& defaults,
                  const state::activity::destination::DestinationSelection& selection,
                  std::string_view name,
                  const state::build_data::scenarios::Definition& layout) noexcept;

struct EffectiveRegion final {
    std::int32_t index{};
    std::uint16_t arrival{};
    bool reported{};
};

[[nodiscard]] EffectiveRegion effective_region(std::uint64_t sessionId) noexcept;

// staging runs before the commit so State still names the region just left; the body must name the delta's region
[[nodiscard]] EffectiveRegion
planned_region(const state::activity::membership::PendingMutation& mutation,
               std::uint64_t sessionId) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity
