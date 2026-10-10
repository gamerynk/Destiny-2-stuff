#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include "catalog.h"
#include "../coo/objective_service.h"
namespace dawn::state::activity::strike_pact {

inline constexpr std::uint32_t kChase=0x588E5FB9U, kChaseTag=0x80F54FFEU;
inline constexpr std::uint32_t kChaseTriggers=0xFBD01A06U, kChaseTriggersTag=0x80F55018U;
inline constexpr std::uint32_t kChaseEntry=0x8E64DB66U, kChaseEntryTag=0x80F55029U;
inline constexpr int kChaseRegion=16;
inline constexpr std::uint8_t kChaseBubble=2;
inline constexpr std::uint32_t kChaseStateHash=0x95E28335U;
inline constexpr std::uint32_t kChaseEntrySpawnSet=0xC803F1CCU, kChaseFirstRoomSpawnSet=0x4DA08186U;

inline constexpr std::uint8_t kCohortFirstRoom=1, kCohortReinforcements=2, kCohortConflict=3;
inline constexpr std::uint8_t kChaseLastCohort=3;

inline constexpr std::array<Spawn,30> kChaseSpawns{{
    {0,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_cabal_firstroom[0]"},
    {1,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_cabal_firstroom[1]"},
    {2,kChase,1,1,2,2,kCohortFirstRoom,true,-1,0,"sq_cabal_firstroom[2]"},
    {3,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_cabal_firstroom[3]"},
    {4,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_cabal_firstroom[4]"},
    {5,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_cabal_firstroom[5]"},
    {6,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_cabal_firstroom[6]"},
    {10,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_vex_firstroom[0]"},
    {11,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_vex_firstroom[1]"},
    {12,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_vex_firstroom[2]"},
    {13,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_vex_firstroom[3]"},
    {14,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_vex_firstroom[4]"},
    {15,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_vex_firstroom[5]"},
    {16,kChase,1,0,1,1,kCohortFirstRoom,true,-1,0,"sq_vex_firstroom[6]"},
    {7,kChase,1,0,1,1,kCohortReinforcements,true,-1,0,"sq_cabal_reinforcements[0]"},
    {8,kChase,1,0,1,1,kCohortReinforcements,true,-1,0,"sq_cabal_reinforcements[1]"},
    {9,kChase,1,0,1,1,kCohortReinforcements,true,-1,0,"sq_cabal_reinforcements[2]"},
    {17,kChase,1,0,1,1,kCohortReinforcements,true,-1,0,"sq_vex_reinforcements[0]"},
    {18,kChase,1,0,1,1,kCohortReinforcements,true,-1,0,"sq_vex_reinforcements[1]"},
    {19,kChase,1,0,1,1,kCohortReinforcements,true,-1,0,"sq_vex_reinforcements[2]"},
    {20,kChase,1,0,1,1,kCohortConflict,false,-1,0,"sq_conflict_snipers[0]"},
    {21,kChase,1,0,1,1,kCohortConflict,false,-1,0,"sq_conflict_snipers[1]"},
    {22,kChase,1,0,1,1,kCohortConflict,false,-1,0,"sq_conflict_goons_top[0]"},
    {23,kChase,1,0,1,1,kCohortConflict,false,-1,0,"sq_conflict_goons_top[1]"},
    {24,kChase,1,1,2,2,kCohortConflict,false,-1,0,"sq_conflict_goons[0]"},
    {25,kChase,1,1,2,2,kCohortConflict,false,-1,0,"sq_conflict_goons[1]"},
    {26,kChase,1,1,2,2,kCohortConflict,false,-1,0,"sq_conflict_goons[2]"},
    {27,kChase,1,1,2,2,kCohortConflict,false,-1,0,"sq_conflict_goons[3]"},
    {28,kChase,1,0,1,1,kCohortConflict,false,-1,0,"sq_conflict_goons[4]"},
    {29,kChase,1,0,1,1,kCohortConflict,false,-1,0,"sq_conflict_goons[5]"},
}};
static_assert([] { for(const auto& s:kChaseSpawns) {
    if(s.count!=s.loose+s.second || s.count!=s.categories || s.cohort==0 || s.cohort>kChaseLastCohort) { return false; } }
    return true; }());
[[nodiscard]] constexpr const Spawn* chase_spawn(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& s:kChaseSpawns) { if(s.registry==registry && s.source==slot) { return &s; } } return nullptr;
}
struct ChaseEnemyReceipt final {
    std::uint64_t run{}; std::uint32_t actor{},owner{},generation{}; std::uint16_t source{}; std::uint32_t registry{};
    bool valid() const noexcept { return run!=0 && actor!=UINT32_MAX && owner!=UINT32_MAX && generation!=0 && registry==kChase; }
    friend bool operator==(const ChaseEnemyReceipt&,const ChaseEnemyReceipt&)=default;
};

inline constexpr std::uint16_t kLaserType=4;
inline constexpr std::array<std::uint16_t,21> kChaseLasers{{
    31,32,33,34,35,36,37,38,39,40,41,
    42,43,44,45,46,47,48,49,50,51,
}};

// order matters: the per-area seen mask is indexed by position, so reordering re-keys
// a live run's retained bits
inline constexpr std::array<Volume,6> kChaseVolumes{{
    {kChaseEntry,2,"pt_enter_chase",{762.93927F,-1341.95166F,182.702866F},{784.267761F,-1321.81165F,202.702866F}},
    {kChase,138,"pt_sparrow_run",{878.398438F,-1690.88513F,138.23381F},{1276.87634F,-1319.4845F,188.23381F}},
    {kChaseTriggers,11,"pt_sparrow_jump",{879.020752F,-1462.68567F,158.23381F},{979.801697F,-1357.0061F,188.23381F}},
    {kChaseTriggers,10,"pt_trap_section",{1271.9469F,-1505.F,105.F},{1314.10059F,-1458.73682F,126.F}},
    {kChaseTriggers,12,"pt_hard_path",{1401.58948F,-1208.55664F,58.8833237F},{1461.64514F,-1144.99121F,98.8833237F}},
    {kChaseTriggers,13,"pt_easy_path",{1398.18896F,-1212.86804F,12.3773613F},{1471.23657F,-1134.22717F,52.3773613F}},
}};
inline constexpr std::uint16_t kChaseTriggerType=31;
inline constexpr std::array<std::uint16_t,6> kChaseTriggerSensors{{1,126,1,2,4,3}};
static_assert(kChaseVolumes.size()==kChaseTriggerSensors.size());
inline constexpr std::array<std::string_view,6> kChaseStages{{
    "enter_chase","sparrow_run","sparrow_jump","trap_section","hard_path","easy_path",
}};
static_assert(kChaseVolumes.size()==kChaseStages.size());

inline constexpr std::uint16_t kChaseAudience=93;
inline constexpr std::uint16_t kChaseObjectiveSlot=30;
inline constexpr std::uint8_t kChaseTaskGroups=11;

inline constexpr coo::Asset kApFightRoom{kChaseEntry,kChaseEntryTag,47,4};
inline constexpr coo::Asset kApBossRoom{kChaseTriggers,kChaseTriggersTag,47,6};

struct DirectiveStep final { std::uint8_t step; std::uint32_t event; coo::Asset target; std::string_view source; };
inline constexpr std::array<DirectiveStep,3> kChaseSteps{{
    {1,kEliminateHostiles,kApFightRoom,"enter_chase"},
    {2,kMountUp,kApBossRoom,"firstroom_cleared"},
    {3,kFindLeader,kApBossRoom,"sparrow_run|sparrow_jump"},
}};

inline constexpr std::uint8_t kCueEnterChase=10, kCueFirstRoomCleared=11, kCueSparrowJump=13;
} // namespace dawn::state::activity::strike_pact
