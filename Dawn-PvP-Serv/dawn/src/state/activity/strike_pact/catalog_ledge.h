#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include "catalog.h"
#include "../coo/objective_service.h"
namespace dawn::state::activity::strike_pact {

inline constexpr std::uint32_t kLedge=0xA5F083B5U, kLedgeTag=0x80F54E07U;
inline constexpr std::uint32_t kBossApproach=0x73CBF939U, kBossApproachTag=0x80F54E12U;
inline constexpr std::uint32_t kBossRoomPoints=0xC6F46FAFU, kBossRoomPointsTag=0x80F54E1DU;
inline constexpr int kLedgeRegion=0;
inline constexpr std::uint8_t kLedgeBubble=0;
inline constexpr std::uint32_t kLedgeStateHash=0xDA8AE171U;
inline constexpr std::uint32_t kLedgeSpawnSet=0x3360C6BEU;
inline constexpr std::uint8_t kLedgeMusicSection=8;

inline constexpr std::uint8_t kCohortLedgeArrival=1,
    kCohortHarvesterCargo=2,
    kCohortLedgeReinforcements=3,
    kCohortHarvester=4,
    kCohortPrefightSkirmish=5;
inline constexpr std::uint8_t kLedgeLastCohort=5;

inline constexpr std::array<Spawn,43> kLedgeSpawns{{
    {2,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[0]"},
    {3,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[1]"},
    {4,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[2]"},
    {5,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[3]"},
    {6,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[4]"},
    {7,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[5]"},
    {8,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[6]"},
    {9,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[7]"},
    {10,kLedge,1,1,2,2,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[8]"},
    {11,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[9]"},
    {12,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[10]"},
    {13,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[11]"},
    {14,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_cabal_ledge[12]"},
    {22,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[0]"},
    {23,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[1]"},
    {24,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[2]"},
    {25,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[3]"},
    {26,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[4]"},
    {27,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[5]"},
    {28,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[6]"},
    {29,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[7]"},
    {30,kLedge,1,1,2,2,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[8]"},
    {31,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[9]"},
    {32,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[10]"},
    {33,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[11]"},
    {34,kLedge,1,0,1,1,kCohortLedgeArrival,true,-1,0,"sq_vex_ledge[12]"},
    {15,kLedge,1,0,1,1,kCohortHarvesterCargo,true,-1,0,"sq_cabal_reinforcement[0]"},
    {16,kLedge,1,0,1,1,kCohortHarvesterCargo,true,-1,0,"sq_cabal_reinforcement[1]"},
    {17,kLedge,1,0,1,1,kCohortHarvesterCargo,true,-1,0,"sq_cabal_reinforcement[2]"},
    {18,kLedge,1,0,1,1,kCohortHarvesterCargo,true,-1,0,"sq_cabal_reinforcement[3]"},
    {19,kLedge,1,0,1,1,kCohortHarvesterCargo,true,-1,0,"sq_cabal_reinforcement[4]"},
    {35,kLedge,1,0,1,1,kCohortLedgeReinforcements,false,-1,0,"sq_vex_reinforcement[0]"},
    {36,kLedge,1,0,1,1,kCohortLedgeReinforcements,false,-1,0,"sq_vex_reinforcement[1]"},
    {37,kLedge,1,0,1,1,kCohortLedgeReinforcements,false,-1,0,"sq_vex_reinforcement[2]"},
    {38,kLedge,1,0,1,1,kCohortLedgeReinforcements,false,-1,0,"sq_vex_reinforcement[3]"},
    {39,kLedge,1,0,1,1,kCohortLedgeReinforcements,false,-1,0,"sq_vex_reinforcement[4]"},
    // zero loose, one named member: the named pilot owns the vehicle, so a loose request
    // would put a second ship on the ledge
    {20,kLedge,0,0,0,1,kCohortHarvester,false,-1,0,"sq_harvester"},
    // started from the boss section while the player is still on the ledge; a ledge
    // controller must leave this cohort disabled
    {40,kLedge,1,0,1,1,kCohortPrefightSkirmish,true,-1,0,"sq_prefight_skirmish[0]"},
    {41,kLedge,1,0,1,1,kCohortPrefightSkirmish,true,-1,0,"sq_prefight_skirmish[1]"},
    {42,kLedge,1,0,1,1,kCohortPrefightSkirmish,true,-1,0,"sq_prefight_skirmish[2]"},
    {43,kLedge,1,0,1,1,kCohortPrefightSkirmish,true,-1,0,"sq_prefight_skirmish[3]"},
    {44,kLedge,1,0,1,1,kCohortPrefightSkirmish,true,-1,0,"sq_prefight_skirmish[4]"},
    {45,kLedge,1,0,1,1,kCohortPrefightSkirmish,true,-1,0,"sq_prefight_skirmish[5]"},
}};
static_assert([] { for(const auto& s:kLedgeSpawns) {
    if(s.count!=s.loose+s.second || s.registry!=kLedge
       || s.cohort==0 || s.cohort>kLedgeLastCohort) { return false; } }
    return true; }());
[[nodiscard]] constexpr const Spawn* ledge_spawn(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& s:kLedgeSpawns) { if(s.registry==registry && s.source==slot) { return &s; } } return nullptr;
}
struct LedgeEnemyReceipt final {
    std::uint64_t run{}; std::uint32_t actor{},owner{},generation{}; std::uint16_t source{}; std::uint32_t registry{};
    bool valid() const noexcept { return run!=0 && actor!=UINT32_MAX && owner!=UINT32_MAX && generation!=0 && registry==kLedge; }
    friend bool operator==(const LedgeEnemyReceipt&,const LedgeEnemyReceipt&)=default;
};

inline constexpr std::array<std::uint16_t,5> kHarvesterCargoSources{{15,16,17,18,19}};
inline constexpr std::array<std::uint16_t,5> kLedgeReinforcementSources{{35,36,37,38,39}};
[[nodiscard]] constexpr bool reserved_cargo(std::uint16_t source) noexcept {
    for(const auto s:kHarvesterCargoSources) { if(s==source) { return true; } } return false;
}
[[nodiscard]] constexpr std::span<const std::uint16_t> cohort_sources(std::uint8_t cohort) noexcept {
    if(cohort==kCohortHarvesterCargo) { return kHarvesterCargoSources; }
    if(cohort==kCohortLedgeReinforcements) { return kLedgeReinforcementSources; }
    return {};
}

inline constexpr std::uint16_t kHarvesterSource=20;
inline constexpr std::uint8_t kHarvesterSourceType=1;
inline constexpr std::uint16_t kHarvesterPilot=21;
inline constexpr std::uint8_t kHarvesterPilotType=2;
inline constexpr std::uint16_t kHarvesterEntryPath=295, kHarvesterExitPath=296;
inline constexpr std::uint8_t kPointSequenceType=58;

// an empty action group fails the native lookup, it is not a wildcard
inline constexpr std::uint32_t kHarvesterActionDefinition=0x80C0E599U;
inline constexpr std::uint32_t kHarvesterFlightController=0x80FE21CEU;
inline constexpr std::uint32_t kHarvesterActionGroup=0x07EBF354U;
inline constexpr std::uint32_t kHarvesterEnterAction=0x4482A76DU;
inline constexpr std::uint32_t kHarvesterExitAction=0x7D0D39A9U;
inline constexpr std::uint32_t kHarvesterDoors=0x80296344U;
inline constexpr float kDoorsClosed=0.F, kDoorsOpen=1.F;

// third identity is the absent sentinel; 127 is the wire marker byte (logical -1)
inline constexpr std::uint32_t kActionAbsentIdentity=0x811C9DC5U;
inline constexpr std::uint8_t kActionMarkerWire=127;
inline constexpr std::int8_t kActionMarker=-1, kActionMode=-1;
inline constexpr std::uint8_t kEntrySnapValue=0, kExitMoveValue=1;
inline constexpr bool kExitMoveEnabled=true;

inline constexpr std::uint32_t kHarvesterGeneration=1, kHarvesterRetireGeneration=2;
inline constexpr std::uint32_t kHarvesterArrivalRevision=1, kHarvesterDepartureRevision=2;
inline constexpr std::uint32_t kHarvesterDeliveryRevision=1;
inline constexpr std::uint32_t kHarvesterSpawnRevision=1;
inline constexpr std::uint8_t kPathStateLanded=4, kPathStateDeparted=3, kDeliveryStateReleased=0;

inline constexpr std::uint32_t kDoneLedgeCleared=4,
    kDoneArrivalPublished=32,
    kDoneLandingHandled=64,
    kDoneDeliveryRequested=128,
    kDoneRetired=256,
    kDoneDeliveryReleased=512,
    kDoneDeparturePublished=1024;
struct FlightStep final {
    std::uint8_t step;
    std::uint32_t generation,revision,doneBit;
    std::string_view action,gate;
};
inline constexpr std::array<FlightStep,4> kHarvesterFlight{{
    {0,kHarvesterGeneration,kHarvesterArrivalRevision,kDoneArrivalPublished,
     "start sq_harvester, reserve the cargo, then spawn+run: "
     "doors closed, snap to entry, enter action, doors open",
     "ledge_final"},
    {1,kHarvesterGeneration,kHarvesterDeliveryRevision,kDoneLandingHandled|kDoneDeliveryRequested,
     "start the reinforcements, then deliver_squads with the reserved cargo",
     "path revision 1 reaches path state 4"},
    {2,kHarvesterGeneration,kHarvesterDepartureRevision,kDoneDeparturePublished,
     "run: doors closed, move to the exit sequence, exit action",
     "delivery revision 1 reaches delivery state 0 and every cargo row was seen"},
    {3,kHarvesterRetireGeneration,0,kDoneRetired,
     "retire_actor",
     "path revision 2 reaches path state 3 and the departure was published"},
}};
inline constexpr std::uint32_t kDepartureRequires=kDoneDeliveryRequested|kDoneDeliveryReleased;
inline constexpr std::uint32_t kDepartureForbids=kDoneRetired|kDoneDeparturePublished;

inline constexpr std::array<Volume,1> kLedgeVolumes{{
    {kBossRoomPoints,3,"pt_see_tree",{1623.53162F,-1490.50146F,4.F},{1634.58618F,-1475.72461F,24.F}},
}};
[[nodiscard]] constexpr const Volume* ledge_volume(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& v:kLedgeVolumes) { if(v.registry==registry && v.slot==slot) { return &v; } } return nullptr;
}

inline constexpr std::uint16_t kLedgeNoVolume=0xFFFFU;
struct LedgeSensor final {
    std::uint32_t registry; std::uint16_t slot; std::uint8_t type; std::uint16_t volume; std::string_view stage;
};
inline constexpr std::array<LedgeSensor,2> kLedgeSensors{{
    // one player trigger in this area, so see_tree is bit 0 and the monitor bit 1; reordering
    // re-keys a live run's bits
    {kBossRoomPoints,0,31,3,"see_tree"},
    {kLedge,177,30,kLedgeNoVolume,"ledge_final"},
}};
inline constexpr std::int32_t kLedgeMonitorOccupancyValue=0;

inline constexpr std::uint16_t kLedgeObjectiveSlot=0;
inline constexpr std::uint8_t kLedgeObjectiveType=3;
inline constexpr std::uint8_t kLedgeTaskGroups=15;
inline constexpr std::uint8_t kLedgeObjectiveRevision=1;
inline constexpr std::int8_t kLedgeTaskUnassigned=-1;

inline constexpr std::uint16_t kLedgeAudience=176;
inline constexpr std::uint8_t kLedgeAudienceType=70;
inline constexpr std::uint8_t kLedgeAudienceFlags=0, kLedgeAudienceRevision=1;

inline constexpr coo::Asset kApBossRoomApproach{kBossApproach,kBossApproachTag,47,1};

struct LedgeDirectiveStep final { std::uint8_t step; std::uint32_t event; coo::Asset target; std::string_view source; };
inline constexpr std::array<LedgeDirectiveStep,2> kLedgeSteps{{
    {1,kFindLeader,kApBossRoomApproach,"region held"},
    {2,kFindMap,kApBossRoomApproach,"ledge_final"},
}};
inline constexpr std::uint8_t kCueLedgeStart=17, kCueLedgeFinal=18, kCueSeeTree=19;
inline constexpr std::string_view kLedgeClearedStage{"ledge_cleared"};
} // namespace dawn::state::activity::strike_pact
