#include <array>
#include <cstdint>
#include <cstdio>

#include "server/gameplay/group/view_session_route.h"

namespace route = dawn::server::gameplay::group::view_session_route;

int main() {
    constexpr std::uint64_t kCurrent = 0x3125D3904B14E59DULL;
    constexpr std::uint64_t kTarget = 0x84EE49D17B52C61DULL;
    constexpr std::uint64_t kOtherEndpoint = 0x9999999999999999ULL;

    const std::array candidates{
        route::Candidate{kCurrent, 20, true, true},
        route::Candidate{kTarget, 21, true, true},
        route::Candidate{kOtherEndpoint, 99, true, false},
        route::Candidate{0, 100, true, true},
    };

    if (route::resolve(kCurrent, candidates) != kCurrent) {
        std::fputs("explicit transport session was not preserved\n", stderr);
        return 1;
    }
    if (route::resolve(0, candidates) != kTarget) {
        std::fputs("multiplexed view did not select the newest endpoint-local target\n", stderr);
        return 2;
    }
    const std::array noRoute{
        route::Candidate{kCurrent, 20, false, true},
        route::Candidate{kOtherEndpoint, 99, true, false},
    };
    if (route::resolve(0, noRoute) != 0) {
        std::fputs("unadmitted or foreign session was selected\n", stderr);
        return 3;
    }

    std::puts("PASS: multiplexed view routes to the newly admitted public target session");
    return 0;
}
