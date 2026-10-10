#pragma once
#include <array>
#include <string_view>

namespace dawn::state::activity::shared {
inline constexpr std::array<std::string_view,11> kPublicDestinationPackages{
    "edz_freeroam", "pandora_freeroam", "eden_freeroam", "planet_x_freeroam",
    "mercury_freeroam", "tangled_shore_freeroam",
    "dreaming_city_freeroam", "luna_freeroam",
    "city_tower_social_d2", "campaign_social_space_d2", "tangled_shore_social_space"
};
[[nodiscard]] constexpr bool public_destination_package(std::string_view name) noexcept {
    for(const auto candidate:kPublicDestinationPackages) if(name==candidate) return true;
    return false;
}
}
