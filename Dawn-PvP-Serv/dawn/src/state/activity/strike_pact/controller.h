#pragma once
#include "profile.h"
#include "catalog_all.h"
#include "../coo/native_generator_authority.h"
#include "frame.h"
#include "../coo/population_service.h"
#include "../coo/mission_runtime.h"
#include "../coo/stall_diagnostics.h"
#include "../../../middleware/bap/activity_message/combatant_sense.h"
#include "../../../middleware/bap/activity_message/scene_sense.h"
#include "../../../middleware/bap/activity_message/squad_sense.h"
#include "../coo/scene_service.h"
#include "../coo/native_activity_clock.h"
#include <bitset>
#include <cmath>
namespace dawn::state::activity::strike_pact {
[[nodiscard]] inline bool contains(const Volume& volume,Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z)
        && point.x>=volume.min.x && point.x<=volume.max.x && point.y>=volume.min.y
        && point.y<=volume.max.y && point.z>=volume.min.z && point.z<=volume.max.z;
}
class Controller final : private coo::Services, private coo::MissionPorts<Frame> {
public:
    void generator(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
                   std::uint32_t seed,std::uint32_t completed) noexcept;
    coo::CampaignScanRequest scan_request() const noexcept {
        return frame_.campaign && frame_.enabled && !frame_.finished
            ?coo::CampaignScanRequest{lifecycle_.owner(),{0x547F6321U,0x80F474E4U,65,0},frame_.spawnGeneration+1U,frame_.scan}
            :coo::CampaignScanRequest{};
    }
    bool scan_observation(coo::Generation owner,std::uint32_t handle,std::uint32_t serial,coo::ScanPlayback playback,bool participant) noexcept {
        const auto request=scan_request();
        if(!request.enabled() || request.owner!=owner || !frame_.scan.observe(request.generation,handle,serial,playback,participant)) return false;
        ++frame_.revision;return true;
    }
    BossRequest boss_request() const noexcept;
    bool health(const EnemyReceipt&,float) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool select(const coo::script::Views& views,std::uint64_t run) noexcept;
    void position(std::uint64_t run,Point point) noexcept;
    [[nodiscard]] bool player_trigger(std::uint64_t run,std::uint32_t registry,std::uint16_t slot) noexcept;
    [[nodiscard]] bool monitor(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
                               bool any,std::int32_t count,std::int32_t value) noexcept;
    [[nodiscard]] bool submitted(std::uint64_t run,std::uint32_t bank,std::uint8_t row,std::uint32_t generation,std::uint64_t now) noexcept;
    [[nodiscard]] bool admitted(const EnemyReceipt& receipt) noexcept;
    [[nodiscard]] bool costed(std::uint32_t registry,std::uint16_t slot,const TaskCosts& report,
                              std::int8_t& selected,std::uint32_t& known) noexcept;
    [[nodiscard]] bool died(const EnemyReceipt& receipt) noexcept;
    [[nodiscard]] bool combatant(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
        const middleware::bap::activity_message::combatant_sense::Output& report) noexcept;
    void scene(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
        const middleware::bap::activity_message::scene_sense::Output& report) noexcept;
    void squad(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
        const middleware::bap::activity_message::squad_sense::Output& report) noexcept;
    bool readiness(const EnemyReceipt& receipt,coo::EnemyReadiness value) noexcept { return population_.observe(receipt,value); }
    void capacity(coo::PopulationCapacity value) noexcept { population_.capacity(value); }
    coo::PopulationCapacity capacity() const noexcept { return population_.capacity(); }
    template<class Visit> void pending_enemies(Visit visit) const noexcept { population_.pending(visit); }
    template<class Visit> void background_diagnostics(Visit visit) const noexcept {
        for(std::size_t i=0;i<kAllSpawns.size();++i) {
            if(!population_.enabled(i)) { continue; }
            auto detail=population_.missing(i,expected_actors(kAllSpawns[i]),false);
            detail.asset={kAllSpawns[i].registry,0,1,kAllSpawns[i].source};visit(i,detail);
        }
    }
    [[nodiscard]] const Frame& frame() const noexcept { return frame_; }
    coo::StallDetail missing(const coo::CommandSpec&) const noexcept;
    [[nodiscard]] const coo::script::GraphView* graph() const noexcept { return views_ && frame_.section<views_->phases.size()?views_->phases[frame_.section]:nullptr; }
    [[nodiscard]] auto step_state(std::size_t i) const noexcept { return executor_.step_state(i); }
    [[nodiscard]] Frame update(std::uint64_t run,std::uint64_t now,bool ready,int region) noexcept;
    [[nodiscard]] const auto& seen() const noexcept { return seen_; }
    [[nodiscard]] coo::Diagnostics diagnostics() const noexcept { return executor_.diagnostics(); }
    [[nodiscard]] std::uint32_t timeouts() const noexcept { return dialogue_.timed_out(); }
private:
    bool publish(const coo::Command&) noexcept override;
    void cancel(const coo::Command&) noexcept override {}
    void update_module(std::uint32_t,const coo::MissionInput&,Frame&) noexcept override;
    std::uint32_t observations(std::uint64_t,const Frame& frame) noexcept override { return frame.checked?1U:0U; }
    bool entered(coo::Asset asset) const noexcept;
    bool observed(const coo::CommandSpec&) const noexcept;
    bool cleared(std::uint32_t cohort) const noexcept;
    bool ready(std::uint32_t cohort) const noexcept;
    void enable(std::uint32_t cohort) noexcept;
    void project_services() noexcept;
    bool boss_command(const coo::Command&) noexcept;
    void advance_boss() noexcept;
    void boss_health(const middleware::bap::activity_message::combatant_sense::Output&) noexcept;
    void boss_lasers(std::uint8_t room,bool on) noexcept;
    bool boss_observed(std::uint32_t argument) const noexcept;
    struct SceneCommand final { std::uint32_t generation{};bool stop{};std::uint8_t eventCount{};std::array<std::uint32_t,32> events{}; };
    struct SceneOwner final { coo::Token token{};bool valid() const noexcept { return token.run!=0 && token.incarnation!=0; } };
    coo::SceneService<SceneCommand,SceneOwner,1> bossScene_;
    coo::CombatantState bossActor_{};
    EnemyReceipt bossEnemy_{};
    bool bossSeen_{},bossRemoved_{},bossEnded_{};
    std::uint32_t bossAlive_{};
    std::uint64_t laserDeadline_{};
    bool laserHigh_{};
    const coo::script::Views* views_{};
    std::uint64_t run_{},now_{};
    coo::NativeActivityClock clock_{};
    int region_{-1};
    std::uint32_t publicationGeneration_{};
    bool started_{},landingSeen_{};
    bool ledgeFinalSeen_{};
    std::bitset<32> dialogueSubmitted_{};
    std::array<std::uint64_t,32> voiceEnds_{};
    coo::LifecycleService lifecycle_{};
    coo::ObjectiveService objectives_{};
    coo::PopulationService<EnemyReceipt,kAllSpawns.size(),4> population_;
    std::array<TaskCosts,kAllSpawns.size()> costs_{};
    coo::MissionRuntime composition_;
    coo::Executor executor_;
    coo::DialogueService<32> dialogue_;
    coo::ActorProgramService harvester_;
    Frame frame_;
    std::bitset<kAllVolumes.size()> seen_{};
};
} // namespace dawn::state::activity::strike_pact
