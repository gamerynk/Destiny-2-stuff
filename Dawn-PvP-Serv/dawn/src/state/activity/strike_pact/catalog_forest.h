#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include "catalog.h"
namespace dawn::state::activity::strike_pact {

inline constexpr std::uint32_t kForest=0x2763EC91U, kForestTag=0x80F550B8U;
inline constexpr std::uint32_t kForestArea=0xA9350228U, kForestAreaTag=0x80F550C9U;
inline constexpr std::uint32_t kForestExit=0x7E558786U, kForestExitTag=0x80F550DAU;
inline constexpr int kForestRegion=72;
inline constexpr std::uint32_t kForestStateHash=0xA522CBE0U;
inline constexpr std::uint8_t kForestMusicSection=2;

inline constexpr std::uint16_t kMapGenerator=30;
inline constexpr std::uint8_t kMapGeneratorType=37;
inline constexpr std::uint32_t kMapGeneratorComponent=0x80804EF6U;

inline constexpr std::uint8_t kGeneratorOverrideSeed=1, kGeneratorOverrideAnchors=4,
    kGeneratorOverrideEnabled=8;
inline constexpr std::uint8_t kGeneratorOverrides=
    kGeneratorOverrideSeed|kGeneratorOverrideAnchors|kGeneratorOverrideEnabled;

// the generator's authored default is zero; a host leaving the -1 sentinel places no encounters
inline constexpr std::int32_t kEncounterPlacementBudget=6;
inline constexpr std::array<std::int32_t,5> kGeneratorValues{
    kEncounterPlacementBudget,-1,-1,-1,-1};

inline constexpr std::uint32_t kGeneratorSeedMask=0x7FFFFFFFU;

struct GeneratorAnchor final { std::int8_t column,height; float progress; bool enabled; };
// only the exit carries progress 1; with all at 0 the solver has no forward gradient
inline constexpr std::array<GeneratorAnchor,4> kGeneratorAnchors{{
    {2,0,0.F,true},
    {0,2,0.F,true},
    {2,0,1.F,true},
    {0,2,0.F,true},
}};

struct HashSwitch final { std::uint32_t key,value; };
inline constexpr std::uint32_t kForestPopulationSelector=0x050C5D2EU;
inline constexpr std::array<HashSwitch,4> kForestHashSwitches{{
    {0x67AF9045U,kForestPopulationSelector},
    {0x0D979BCDU,kForestPopulationSelector},
    {0xAD3780EEU,kForestPopulationSelector},
    {0x89567586U,kForestPopulationSelector},
}};

inline constexpr std::uint8_t kForestLastCohort=1;
inline constexpr std::array<Spawn,10> kForestSpawns{{
    {6,kForest,1,0,1,1,1,true,-1,0,"sq_portal_guard[0]"},
    {7,kForest,1,0,1,1,1,true,-1,0,"sq_portal_guard[1]"},
    {8,kForest,1,0,1,1,1,true,-1,0,"sq_portal_guard[2]"},
    {9,kForest,1,0,1,1,1,true,-1,0,"sq_portal_guard[3]"},
    {10,kForest,1,0,1,1,1,true,-1,0,"sq_portal_guard[4]"},
    {11,kForest,1,0,1,1,1,true,-1,0,"sq_portal_guard[5]"},
    {12,kForest,1,0,1,1,1,true,-1,0,"sq_portal_sniper[0]"},
    {13,kForest,1,0,1,1,1,true,-1,0,"sq_portal_sniper[1]"},
    {14,kForest,1,0,1,1,1,true,-1,0,"sq_portal_sniper[2]"},
    {15,kForest,1,0,1,1,1,true,-1,0,"sq_portal_sniper[3]"},
}};
static_assert([] { for(const auto& s:kForestSpawns) { if(s.count!=s.loose+s.second) { return false; } } return true; }());
[[nodiscard]] constexpr const Spawn* forest_spawn(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& s:kForestSpawns) { if(s.registry==registry && s.source==slot) { return &s; } } return nullptr;
}

inline constexpr std::uint16_t kForestShieldWall=17;
inline constexpr std::uint8_t kForestShieldWallType=23;
inline constexpr std::uint8_t kShieldPresent=1, kShieldRemoved=0;

inline constexpr std::uint16_t kForestAudience=28;
inline constexpr std::uint8_t kForestAudienceType=70;
inline constexpr std::uint8_t kForestAudienceFlags=0, kForestAudienceRevision=1;

inline constexpr std::uint16_t kForestObjectiveSlot=16;
inline constexpr std::uint8_t kForestObjectiveGroups=3, kForestObjectiveRevision=1;

inline constexpr std::array<Volume,7> kForestVolumes{{
    {kForest,45,"pt_load_post",{-648.999878F,-37.2999573F,-106.500046F},{-433.999847F,176.442703F,-6.5F}},
    {kForest,46,"pt_begin",{-834.311829F,-430.939789F,-30.6876411F},{-782.811829F,-415.439789F,14.3123589F}},
    {kForest,52,"tv_end",{-632.603455F,23.8969574F,-59.4608154F},{-465.421295F,45.0812836F,-29.4608154F}},
    {kForestArea,3,"pt_see_portal",{-678.842346F,-86.5864868F,-112.000008F},{-376.238556F,189.364792F,-12.F}},
    {kForestArea,6,"pt_reached_portal",{-639.F,-5.5F,-63.8789711F},{-465.730774F,148.646744F,-23.8789673F}},
    {kForestArea,7,"pt_start_forest",{-864.702576F,-382.185211F,-34.F},{-678.61908F,-251.627258F,6.00000381F}},
    {kForestExit,2,"pt_leaving_forest",{-547.F,152.F,-39.F},{-529.F,179.F,-19.F}},
}};
[[nodiscard]] constexpr const Volume* forest_volume(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& v:kForestVolumes) { if(v.registry==registry && v.slot==slot) { return &v; } } return nullptr;
}

inline constexpr std::uint16_t kNoVolume=0xFFFFU;
struct Sensor final {
    std::uint32_t registry; std::uint16_t slot; std::uint8_t type; std::uint16_t volume; std::string_view stage;
};
inline constexpr std::array<Sensor,8> kForestSensors{{
    {kForest,31,31,46,"begin"},
    {kForest,33,31,45,"load_post"},
    {kForestArea,1,31,3,"see_portal"},
    {kForestArea,2,31,6,"reached_portal"},
    {kForestExit,1,31,2,"leaving_forest"},
    {kForestArea,0,31,7,"start_forest"},
    {kForest,32,30,52,"forest_end"},
    {kLighthouse,4,30,kNoVolume,"ifb_arrival"},
}};
// an omitted filter selects the native registered players; the int32 is echoed, not a threshold
inline constexpr std::int32_t kMonitorOccupancyValue=0;

struct ForestStep final {
    std::uint8_t step; std::uint32_t event; std::uint32_t targetRegistry; std::uint16_t targetSlot; std::string_view source;
};
inline constexpr std::array<ForestStep,4> kForestSteps{{
    {0,kTrackCabal,0,0,"region held"},
    {1,kDisableBarrier,kForestArea,5,"see_portal or forest_end"},
    {2,kTraverseForest,kForestArea,5,"portal_defense_cleared"},
    {3,kTraverseForest,kForestExit,7,"leaving_forest"},
}};

inline constexpr std::uint32_t kForestEntranceSpawnSet=0xFEB62946U;
inline constexpr std::uint32_t kForestExitSpawnSet=0x54A5A748U;

inline constexpr std::uint8_t kNavigationPointType=47;
inline constexpr std::uint16_t kForestPortalAreaPoint=5;
inline constexpr std::uint16_t kForestPortalExitPoint=7;
} // namespace dawn::state::activity::strike_pact
