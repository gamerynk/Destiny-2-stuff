#include <cmath>
#include <cstdio>

#include "state/activity/crucible/native_maps.h"

namespace catalog = dawn::state::activity::crucible;

namespace {

bool require(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
    return condition;
}

} // namespace

int main() {
    const auto maps = catalog::native_maps();
    bool passed = require(maps.size() == 31, "all installed PvP maps exposed");
    std::size_t combatCount = 0;
    std::size_t teamCount = 0;
    std::size_t arrivalCount = 0;
    for (const auto& map : maps) {
        passed &= require(catalog::find_native_map(map.scenarioName) == &map,
                          "scenario-name lookup resolves the catalog row");
        passed &= require(catalog::find_native_map(map.scenarioTag) == &map,
                          "scenario-tag lookup resolves the catalog row");
        const auto combat = catalog::combat_spawns(map);
        const auto team = catalog::team_spawns(map);
        const auto arrivals = catalog::arrival_points(map);
        passed &= require(!combat.empty() && combat.size() == map.combatSpawnCount,
                          "combat spawn range is complete");
        passed &= require(!team.empty() && team.size() == map.teamSpawnCount,
                          "team spawn range is complete");
        passed &= require(arrivals.size() == map.arrivalPointCount,
                          "arrival range is complete");
        for (const auto& spawn : combat) {
            passed &= require(!spawn.authoredName.empty() && spawn.placedTag != 0
                                  && spawn.objectTag != 0 && spawn.registryTag != 0
                                  && spawn.slotIndex < 1280,
                              "combat spawn retains source and roster provenance");
            for (float axis : spawn.position) {
                passed &= require(std::isfinite(axis), "combat position is finite");
            }
            for (float axis : spawn.rotation) {
                passed &= require(std::isfinite(axis), "combat rotation is finite");
            }
        }
        combatCount += combat.size();
        for (const auto& spawn : team) {
            passed &= require(spawn.slotIndex < 1280 && spawn.placedTag != 0
                                  && spawn.team >= -1 && spawn.team <= 12,
                              "team spawn retains source and native team identity");
        }
        teamCount += team.size();
        arrivalCount += arrivals.size();
    }
    passed &= require(combatCount == 2166 && teamCount == 1254 && arrivalCount == 31,
                      "all spawn and arrival records are reachable");
    const auto* javelin = catalog::find_native_map("pvp_shaft");
    passed &= require(javelin != nullptr && javelin->displayName == "Javelin-4"
                          && javelin->combatSpawnCount == 35 && javelin->teamSpawnCount == 50
                          && javelin->arrivalPointCount == 2,
                      "Javelin-4 native fixture");
    const auto* altar = catalog::find_native_map("pvp_ness");
    passed &= require(altar != nullptr && altar->displayName == "Altar of Flame"
                          && altar->combatSpawnCount == 61,
                      "Altar of Flame native fixture");
    const auto* widows = catalog::find_native_map("pvp_wilderness_town_2");
    passed &= require(widows != nullptr && widows->displayName == "Widow's Court"
                          && widows->combatSpawnCount == 38 && widows->arrivalPointCount == 0,
                      "Widow's Court combat-only arrival fixture");
    passed &= require(catalog::find_native_map("not_an_installed_map") == nullptr
                          && catalog::find_native_map(0U) == nullptr,
                      "missing map lookup stays empty");
    if (javelin != nullptr) {
        auto invalid = *javelin;
        invalid.firstCombatSpawn = 99999;
        invalid.firstTeamSpawn = 99999;
        invalid.firstArrivalPoint = 99999;
        passed &= require(catalog::combat_spawns(invalid).empty()
                              && catalog::team_spawns(invalid).empty()
                              && catalog::arrival_points(invalid).empty(),
                          "out-of-range synthetic map cannot index generated banks");
    }
    if (passed) {
        std::puts("PASS: installed Crucible map and spawn catalog");
    }
    return passed ? 0 : 1;
}
