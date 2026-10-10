#pragma once
#include "../coo/population_readiness_request.h"
#include "frame.h"
#include "catalog.h"
#include "../coo/population_service.h"
#include "../../../middleware/bap/activity_message/combatant_sense.h"
#include "../../../middleware/bap/activity_message/scene_sense.h"
#include "../../../middleware/bap/activity_message/squad_sense.h"
namespace dawn::state::activity::strike_pact {
[[nodiscard]] coo::ReadinessRequest<EnemyReceipt> readiness_request(std::uint64_t now) noexcept;
void observe_capacity(std::uint64_t,coo::PopulationCapacity) noexcept;
[[nodiscard]] bool prepare(std::uint64_t run,bool selected,bool campaign=false) noexcept;
[[nodiscard]] Frame snapshot(std::uint64_t run,std::uint64_t now,bool ready,int region) noexcept;
[[nodiscard]] std::uint64_t native_run() noexcept;
BossRequest boss_request() noexcept;
bool observe_health(const EnemyReceipt&,float) noexcept;
void observe_player_trigger(std::uint64_t run,std::uint32_t registry,std::uint16_t slot) noexcept;
struct Publication final {
    std::uint64_t run{};
    std::uint32_t revision{};
    // the graph owns host timers, so an enabled mission must keep ticking when idle
    bool enabled{};
};
[[nodiscard]] Publication publication() noexcept;
[[nodiscard]] int region_rank(int region) noexcept;
void observe_generator(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
                       std::uint32_t seed,std::uint32_t completed) noexcept;
[[nodiscard]] bool observe_admission(const EnemyReceipt& receipt) noexcept;
[[nodiscard]] bool observe_death(const EnemyReceipt& receipt) noexcept;
void observe_costs(std::uint32_t registry,std::uint16_t slot,const TaskCosts& report) noexcept;
void observe_monitor(std::uint32_t registry,std::uint16_t slot,bool any,
                     std::int32_t count,std::int32_t value) noexcept;
void observe_combatant(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::combatant_sense::Output& report) noexcept;
void observe_scene(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::scene_sense::Output& report) noexcept;
void observe_squad(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::squad_sense::Output& report) noexcept;
void observe_readiness(const EnemyReceipt&,coo::EnemyReadiness) noexcept;
void observe_submission(std::uint64_t run,std::uint32_t definition,std::int64_t offset,
    std::uint32_t bank,std::uint8_t row,std::uint32_t generation) noexcept;
coo::CampaignScanRequest scan_request() noexcept;
void observe_scan(coo::Generation,std::uint32_t,std::uint32_t,coo::ScanPlayback,bool) noexcept;
}
