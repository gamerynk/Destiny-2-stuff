#include <cstdint>
#include <cstdio>

#include "steam/interfaces/methods/matchmaking_policy.h"

namespace policy = dawn::steam::interfaces::methods::matchmaking_policy;

namespace {

bool expect(bool condition, const char* name) noexcept {
    if (condition) {
        return true;
    }
    std::fprintf(stderr, "FAIL: %s\n", name);
    return false;
}

} // namespace

int main() {
    bool ok = true;
    ok = expect(policy::central_lobby_id(0x0184000000000001ULL),
                "central fireteam lobby routes to realtime store")
         && ok;
    ok = expect(!policy::central_lobby_id(0xD53D82631645CAF3ULL),
                "region-408 direct session stays local")
         && ok;
    ok = expect(!policy::central_lobby_id(0x60F618222603E973ULL),
                "region-24 direct session stays local")
         && ok;
    ok = expect(!policy::central_lobby_id(0), "absent session is not central") && ok;
    if (!ok) {
        return 1;
    }
    std::puts("PASS: Steam central-lobby/direct-managed-session routing policy");
    return 0;
}
