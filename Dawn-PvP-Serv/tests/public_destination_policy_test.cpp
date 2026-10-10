#include "state/activity/shared/public_destination_policy.h"
#include <cstdio>
int main(){
    namespace p=dawn::state::activity::shared;
    unsigned checks=0;
    for(const auto name:p::kPublicDestinationPackages){++checks;if(!p::public_destination_package(name))return 1;}
    for(const auto name:{"", "orbit", "mission_towerfall", "city_tower_d2_red_war_lap", "raid_gluttony_0",
            "trials_social_space_d2", "trophy_hall_freeroam", "sundial_freeroam", "mercury_lost_woods_freeroam",
            "tangled_shore_reveal_freeroam", "pvp_ness", "prophecy", "unknown_freeroam", "fake_social"}){
        ++checks;if(p::public_destination_package(name)){std::printf("FAIL: private/unknown %s\n",name);return 1;}
    }
    std::printf("PASS: %u public destination classification cases\n",checks);return 0;
}
