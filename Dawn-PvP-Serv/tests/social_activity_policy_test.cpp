#include <cstdio>
#include <cstdlib>
#include "middleware/content/social_activity_policy.h"
namespace p=dawn::middleware::content::social_activity_policy;
int main(){
    constexpr std::array<p::Definition,8> expected{{
        {0xE8ABA41B,20,"city_tower_social_d2"},{0x4E0FA012,22,"campaign_social_space_d2"},
        {0x5D813A4C,23,"campaign_social_space_d2"},{0x13B3BA49,24,"campaign_social_space_d2"},
        {0xD873EF56,25,"campaign_social_space_d2"},{0x4CEF34A6,26,"campaign_social_space_d2"},
        {0x34B43AB2,44,"tangled_shore_social_space"},{0x59905E37,84,"city_tower_social_d2"}}};
    unsigned count{};
    const auto check=[&](bool v){if(!v){std::puts("FAIL: exact social definition policy");std::exit(1);}++count;};
    check(p::kDefinitions.size()==expected.size());
    for(const auto& row:expected){
        check(p::find(row.hash)!=nullptr);
        for(const auto& other:expected)
            check(p::matches(row.hash,other.index,other.package)==(row.index==other.index));
        check(!p::matches(row.hash,row.index,"mission_nadir"));
        check(!p::matches(row.hash,-1,row.package));
    }
    for(auto hash:{0U,0x04F12A8AU,0xE8ABA41AU,0xDEE2DFE6U,0x11882989U,0x12345678U})check(!p::find(hash));
    std::printf("PASS: %u exact social definition and cross-variant isolation checks\n",count);
}
