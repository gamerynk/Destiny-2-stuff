#pragma once

#include <cstdint>
#include <string_view>

#include "../../../../../state/build_data/scenarios/definition.h"
#include "internal.h"

namespace dawn::server::bap::encrypted::push::activity::mission_roster {

struct Report final {
    unsigned destination{};
    unsigned pinned{};
    unsigned missing{};
    unsigned full{};
    std::uint32_t lastMissing{};
};

// on false the caller must publish the ordinary roster and run no mission; a mission body naming an unregistered object is dropped in silence
[[nodiscard]] bool compose(const state::build_data::scenarios::Definition& layout,
                           std::string_view destination,
                           Scratch& scratch,
                           message::Roster& roster,
                           Report& report) noexcept;

} // namespace dawn::server::bap::encrypted::push::activity::mission_roster
