#include "../dawn/src/client/ui/weapons/weapon_acquire_request.h"
#include <cstdio>

int main() {
    using namespace dawn::client::ui::weapons::acquire_client::detail;
    Selection previous{};
    previous.requestId=123; previous.characterSoid=42; previous.weaponHash=100;
    previous.count=2; previous.plugs[0]=5; previous.plugs[1]=6;
    const std::array<std::uint32_t,2> same{5,6}, changed{5,7};
    if (!reuse_request(true,previous,42,100,same)) return 1;
    if (reuse_request(false,previous,42,100,same)) return 2;
    if (reuse_request(true,previous,43,100,same)
        || reuse_request(true,previous,42,101,same)
        || reuse_request(true,previous,42,100,changed)
        || reuse_request(true,previous,42,100,std::span(same).first(1))) return 3;
    previous.requestId=0;
    if (reuse_request(true,previous,42,100,same)) return 4;
    std::puts("Weapon acquisition retry identity checks passed.");
    return 0;
}
