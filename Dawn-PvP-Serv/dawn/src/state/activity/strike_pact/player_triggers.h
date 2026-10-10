#pragma once
#include "catalog_all.h"
namespace dawn::state::activity::strike_pact {
struct PlayerTrigger final { std::uint32_t registry;std::uint16_t sensor,volume; };
// authored type-31 sensors paired with their type-60 volumes in scenario 80F54AE7
inline constexpr PlayerTrigger kPlayerTriggers[]{
    {kOpening,67,73},{kOpening,66,71},{kLighthouse,0,10},
    {kLighthouse,3,5},{kLighthouse,1,11},
    {kForest,33,45},{kForest,31,46},
    {0xA9350228U,1,3},{0xA9350228U,2,6},{0xA9350228U,0,7},
    {0x7E558786U,1,2},{0x8E64DB66U,1,2},{0x588E5FB9U,126,138},
    {0xFBD01A06U,1,11},{0xFBD01A06U,2,10},{0xFBD01A06U,4,12},{0xFBD01A06U,3,13},
    {0xC6F46FAFU,0,3},{0x73CBF939U,0,4},
};
[[nodiscard]] constexpr const PlayerTrigger* player_trigger(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& trigger:kPlayerTriggers) {
        if(trigger.registry==registry && trigger.sensor==slot) { return &trigger; }
    }return nullptr;
}
}
