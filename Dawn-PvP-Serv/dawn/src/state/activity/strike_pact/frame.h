#pragma once
#include "../coo/campaign_scan.h"
#include "../coo/dialogue_service.h"
#include "../coo/objective_service.h"
#include "../coo/lifecycle_service.h"
#include "catalog_tasks.h"
#include "../coo/actor_program_service.h"
namespace dawn::state::activity::strike_pact {
struct BossPublication final {
    std::uint32_t sceneGeneration{},laserRevision{1};
    std::uint8_t room{},fights{},retreated{},cleared{},arrived{},laserRooms{},laserHighRooms{},laserObjects{};
    bool prepared{},sceneFinished{},immune{},dead{},healthObserved{};
    bool minotaurSpawned{},minotaurDead{};
    float health{1.F};
};
struct BossRequest final {
    coo::Generation owner{};EnemyReceipt enemy{};
    std::uint8_t stage{};std::uint32_t revision{};bool requested{};
};
struct Frame final {
    bool campaign{};coo::CampaignScan scan{};
    bool enabled{},checked{},finished{},populationFault{},services{};
    std::uint8_t section{};
    std::uint32_t spawnGeneration{},objective{},revision{};
    // one bit per global cohort in the combined source table; bit 0 is never used
    std::uint64_t cohorts{};
    // a device carries a body only once the mission owns it, so untouched barriers keep authored state
    std::uint32_t devices{},devicesOpen{};
    bool portalActive{};
    // nonzero once the Forest generator is active; fixed for the run so reattach republishes the same seed
    std::uint32_t generatorSeed{};
    bool firstForestAreaComplete{};
    std::uint64_t activityTime{},endEpoch{};
    std::uint8_t musicCandidate{255};
    std::uint8_t enemyVariant{};
    std::uint16_t region{120};
    // a death respawns on the lifetime's spawn set, so each region names its own once selected
    std::uint32_t checkpointSliceSet{},checkpointSpawnSet{};
    // task group per source, biased by one so a default frame publishes evaluate-only, not group zero
    std::array<std::uint8_t,kAllSpawns.size()> taskPlusOne{};
    std::array<std::uint32_t,32> generations{};
    std::uint8_t activeRow{coo::kNoDialogue};
    coo::ObjectiveState presentation{};
    coo::CompletionPublication completion{};
    coo::ActorPublication harvester{};
    BossPublication boss{};
};
}
