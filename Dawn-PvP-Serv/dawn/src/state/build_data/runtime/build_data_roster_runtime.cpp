#include "../runtime.h"
#include "../scenarios/scenario_catalog.h"

namespace dawn::state::build_data {

bool find_roster_group(std::size_t index, scenarios::RosterGroup& group) noexcept {
    group = {};
    return scenario_layouts_ready() && scenarios::group(index, group);
}

bool find_roster_group_by_key(std::uint32_t key,
                              std::uint32_t objectTag,
                              scenarios::RosterGroup& group) noexcept {
    group = {};
    return scenario_layouts_ready() && scenarios::group_by_key(key, objectTag, group);
}

} // namespace dawn::state::build_data
