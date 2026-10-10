#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

#include "steam/runtime/social/social_delta.h"

namespace {

namespace delta = dawn::steam::runtime::social::delta;
using dawn::state::social::Relationship;
using dawn::state::social::RemoteAccount;

template <std::size_t Capacity>
void assign(std::array<char, Capacity>& output, std::string_view value) {
    output = {};
    std::copy(value.begin(), value.end(), output.begin());
}

[[nodiscard]] RemoteAccount baseline() {
    RemoteAccount remote{};
    remote.platformId = 0x0110000100000001ULL;
    remote.lobbyId = 0x0184000000000001ULL;
    remote.relationship = Relationship::friendAccount;
    remote.personaState = 1;
    remote.appId = 1085660;
    assign(remote.displayName, "Remote");
    assign(remote.location, "In Orbit");
    assign(remote.steamDisplay, "#status");
    assign(remote.connect, "/connect:token");
    assign(remote.playerGroup, "lobby");
    assign(remote.playerGroupSize, "1");
    return remote;
}

[[nodiscard]] bool require(bool condition, const char* name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

} // namespace

int main() {
    const RemoteAccount original = baseline();
    RemoteAccount changed = original;
    bool passed = true;

    passed &= require(delta::persona_change_flags(&original, &changed) == 0,
                      "identical snapshot has no persona delta");
    passed &= require(!delta::rich_presence_changed(&original, &changed),
                      "identical snapshot has no presence delta");

    assign(changed.playerGroupSize, "2");
    passed &= require(delta::persona_change_flags(&original, &changed) == 0,
                      "fireteam-size change is not a persona or name change");
    passed &= require(delta::rich_presence_changed(&original, &changed),
                      "fireteam-size change uses rich-presence callback 336");

    changed = original;
    changed.selectedCharacterSoid = 44;
    changed.shipInstanceSoid = 55;
    changed.profileRevision = 66;
    changed.power = 1060;
    passed &= require(delta::persona_change_flags(&original, &changed) == 0,
                      "Destiny profile delta is not a Steam persona delta");
    passed &= require(!delta::rich_presence_changed(&original, &changed),
                      "Destiny profile delta is not Steam rich presence");

    changed = original;
    assign(changed.displayName, "Renamed");
    passed &= require(delta::persona_change_flags(&original, &changed) == delta::kName,
                      "name change carries only the Steam name flag");

    changed = original;
    changed.personaState = 0;
    passed &= require(delta::persona_change_flags(&original, &changed)
                          == (delta::kStatus | delta::kGoneOffline),
                      "offline transition carries status and gone-offline flags");

    changed = original;
    changed.relationship = Relationship::blocked;
    passed &= require(delta::persona_change_flags(&original, &changed) == delta::kRelationship,
                      "relationship change carries only the relationship flag");

    constexpr std::int32_t expectedAdded =
        delta::kName | delta::kStatus | delta::kComeOnline | delta::kGamePlayed
        | delta::kRelationship | delta::kNameFirstSet;
    passed &= require(delta::persona_change_flags(nullptr, &original) == expectedAdded,
                      "new remote carries exact initial persona flags");
    passed &= require(delta::rich_presence_changed(nullptr, &original),
                      "new remote publishes initial rich presence");

    constexpr std::int32_t expectedRemoved =
        delta::kStatus | delta::kGoneOffline | delta::kGamePlayed | delta::kRelationship;
    passed &= require(delta::persona_change_flags(&original, nullptr) == expectedRemoved,
                      "removed remote carries exact terminal persona flags");

    if (passed) {
        std::puts("PASS: Steam social delta callback contract");
        return 0;
    }
    return 1;
}
