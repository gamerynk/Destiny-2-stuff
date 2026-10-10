#include <Windows.h>
#include "runtime.h"
#include "controller.h"
#include "../mission_host/mission_host.h"
#include "../../../core/filesystem/path.h"
#include "../../../core/logging/log.h"
#include <cstdio>
#include <mutex>
namespace dawn::state::activity::strike_pact {
namespace {
std::mutex mutex;
Controller controller;
coo::ReadinessSchedule readinessSchedule;
coo::StallDiagnostics stalled,background;
std::uint32_t publishedRevision{UINT32_MAX};
std::uint64_t selectedRun{};
std::array<std::unique_ptr<coo::script::MissionDocument>,2> documents;
std::bitset<kAllVolumes.size()> loggedVolumes;
std::uint32_t loggedActive{UINT32_MAX},loggedTimeouts{};
std::uint8_t loggedSection{UINT8_MAX};
coo::Phase loggedPhase{coo::Phase::idle};
bool loggedBank{};
void log(std::string_view message) noexcept {
    core::log::write(core::log::Channel::server,core::log::Level::info,message);
}
bool load(bool campaign) noexcept {
    auto& document=documents[campaign?1:0];
    static std::array<std::once_flag,2> once;
    std::call_once(once[campaign?1:0],[&] {
        std::string error;
        try {
            core::path::Buffer path{};
            if(!core::path::artifact_file(campaign?L"scripts\\mission_pact.lua":L"scripts\\strike_pact.lua",path)) { error="cannot resolve Restoration/scripts path"; }
            else { document=coo::script::MissionDocument::read(std::filesystem::path(path.chars.data()),kProfile,error); }
            if(document && !valid_document(document->views())) { document.reset();error="strike_pact native binding validation failed"; }
        } catch(const std::exception& exception) { error=exception.what(); }
        std::array<char,768> line{};
        if(document) {
            std::snprintf(line.data(),line.size(),"ev=coo_script mission=%s result=loaded format=lua fnv1a64=%016llX path=Restoration/scripts/%s.lua scope=mission reload=next_process",
                campaign?"mission_pact":"strike_pact",static_cast<unsigned long long>(document->fingerprint()),campaign?"mission_pact":"strike_pact");
        } else { std::snprintf(line.data(),line.size(),"ev=coo_script mission=%s result=failed reason=\"%.*s\"",campaign?"mission_pact":"strike_pact",static_cast<int>((std::min)(error.size(),std::size_t{500})),error.data()); }
        log(line.data());
    });
    return document!=nullptr;
}
void reset_diagnostics() noexcept {
    stalled.reset();background.reset();readinessSchedule.reset();publishedRevision=UINT32_MAX;
    loggedVolumes.reset();loggedActive=UINT32_MAX;loggedSection=UINT8_MAX;loggedTimeouts=0;loggedPhase=coo::Phase::idle;loggedBank=false;
}
}
bool prepare(std::uint64_t run,bool selected,bool campaign) noexcept {
    const auto& document=documents[campaign?1:0];
    const std::lock_guard lock(mutex);
    if(run!=mission_host::run()) { return false; }
    if(!selected) {
        if(selectedRun!=0) {
            std::array<char,192> line{};
            std::snprintf(line.data(),line.size(),"ev=strike_pact stage=reset run=%llu reason=destination_changed",
                static_cast<unsigned long long>(selectedRun));log(line.data());
        }
        controller.reset();readinessSchedule.reset();reset_diagnostics();selectedRun=0;return false;
    }
    if(!load(campaign)) { return false; }
    if(run!=selectedRun) { reset_diagnostics(); }
    if(!controller.select(document->views(),run)) { return false; }
    selectedRun=run;return true;
}
coo::ReadinessRequest<EnemyReceipt> readiness_request(std::uint64_t now) noexcept {
    const std::lock_guard lock(mutex);
    if(!selectedRun || selectedRun!=mission_host::run() || !mission_host::armed()) { return {}; }
    return readinessSchedule.request<EnemyReceipt, 64>(selectedRun,now,
        [&](auto visit) noexcept { controller.pending_enemies(visit); });
}

void observe_readiness(const EnemyReceipt& receipt,coo::EnemyReadiness value) noexcept {
    const std::lock_guard lock(mutex);if(receipt.run!=selectedRun || receipt.run!=mission_host::run()) { return; }
    static_cast<void>(controller.readiness(receipt,value));
}
void observe_capacity(std::uint64_t run,coo::PopulationCapacity value) noexcept {
    const std::lock_guard lock(mutex);
    if(run && run==selectedRun && run==mission_host::run() && mission_host::armed()) controller.capacity(value);
}
Frame snapshot(std::uint64_t run,std::uint64_t now,bool ready,int region) noexcept {
    const std::lock_guard lock(mutex);
    if(run!=selectedRun || run!=mission_host::run()) { return {}; }
    const auto frame=controller.update(run,now,ready && mission_host::armed(),region);
    const bool sectionChanged=frame.section!=loggedSection;
    if(sectionChanged) { loggedVolumes.reset();loggedSection=frame.section; }
    for(std::size_t i=0;i<kAllVolumes.size();++i) {
        if(controller.seen()[i] && !loggedVolumes[i]) {
            loggedVolumes.set(i);std::array<char,320> line{};const auto& v=kAllVolumes[i];
            std::snprintf(line.data(),line.size(),"ev=strike_pact stage=volume_entered run=%llu registry=%08X type=60 slot=%u name=%.*s evidence=native_player_trigger",
                static_cast<unsigned long long>(run),v.registry,v.slot,static_cast<int>(v.name.size()),v.name.data());log(line.data());
        }
    }
    const auto d=controller.diagnostics();
    if(sectionChanged || d.active!=loggedActive || d.phase!=loggedPhase || controller.timeouts()!=loggedTimeouts) {
        loggedActive=d.active;loggedPhase=d.phase;loggedTimeouts=controller.timeouts();std::array<char,320> line{};
        std::snprintf(line.data(),line.size(),"ev=coo_executor mission=strike_pact scope=mission run=%llu phase=%u active=%08X complete=%08X failure=%u dialogue_timeouts=%u section=%u cohorts=%016llX finished=%u generator_seed=%u",
            static_cast<unsigned long long>(run),static_cast<unsigned>(d.phase),d.active,d.complete,static_cast<unsigned>(d.failure),loggedTimeouts,frame.section,
            static_cast<unsigned long long>(frame.cohorts),frame.finished?1U:0U,frame.generatorSeed);log(line.data());
    }
    if(const auto* graph=controller.graph();graph && d.phase==coo::Phase::running) {
        unsigned lines{};
        for(const auto& binding:graph->commands) {
            if(controller.step_state(binding.step).phase!=coo::StepPhase::active || lines>=8) { continue; }
            const auto& spec=graph->definition.steps[binding.step].commands[binding.command];coo::StallReport report{};
            if(!stalled.observe({run,d.incarnation,binding.step,binding.command},controller.missing(spec),now,report)) { continue; }
            ++lines;std::array<char,512> line{};
            std::snprintf(line.data(),line.size(),"ev=coo_stall mission=strike_pact step=%.*s command=%.*s missing=%s registry=%08X type=%u slot=%u expected=%u actual=%u detail=%u waiting_ms=%llu",
                static_cast<int>(graph->definition.steps[binding.step].name.size()),graph->definition.steps[binding.step].name.data(),
                static_cast<int>(binding.id.size()),binding.id.data(),coo::missing_name(report.detail.missing),report.detail.asset.registry,
                report.detail.asset.type,report.detail.asset.slot,report.detail.expected,report.detail.actual,report.detail.detail,
                static_cast<unsigned long long>(report.waitingMs));log(line.data());
        }
    }
    if(frame.enabled && !frame.finished) {
        unsigned lines{};
        controller.background_diagnostics([&](std::size_t i,coo::StallDetail detail) noexcept {
            coo::StallReport report{};
            if(lines>=8 || !background.observe({run,d.incarnation,static_cast<std::uint8_t>(i/8),static_cast<std::uint8_t>(i%8)},detail,now,report)) { return; }
            ++lines;std::array<char,400> line{};const auto capacity=controller.capacity();
            std::snprintf(line.data(),line.size(),"ev=coo_readiness_stall mission=strike_pact missing=%s registry=%08X type=%u slot=%u expected=%u actual=%u actor=%08X capacity_known=%u allocated=%u maximum=%u waiting_ms=%llu",
                coo::missing_name(detail.missing),detail.asset.registry,detail.asset.type,detail.asset.slot,detail.expected,detail.actual,detail.detail,
                capacity.known?1U:0U,capacity.used,capacity.maximum,static_cast<unsigned long long>(report.waitingMs));log(line.data());
        });
    }
    publishedRevision=frame.revision;
    return frame;
}
Publication publication() noexcept {
    if(!mission_host::armed()) { return {}; }
    const std::lock_guard lock(mutex);
    if(selectedRun==0 || selectedRun!=mission_host::run()) { return {}; }
    return {selectedRun,controller.frame().revision,controller.frame().enabled};
}
int region_rank(int region) noexcept {
    switch(region) {
        case 120: return 0;
        case 72: return 1;
        case 16: return 2;
        case 0: return 3;
        default: return -1;
    }
}
std::uint64_t native_run() noexcept {
    if(!mission_host::armed()) { return 0; }
    const std::lock_guard lock(mutex);return selectedRun==mission_host::run()?selectedRun:0;
}
BossRequest boss_request() noexcept {
    if(!mission_host::armed()) {return {};}
    const std::lock_guard lock(mutex);
    return selectedRun==mission_host::run()?controller.boss_request():BossRequest{};
}
bool observe_health(const EnemyReceipt& enemy,float fraction) noexcept {
    if(!mission_host::armed()) {return false;}
    const std::lock_guard lock(mutex);
    return enemy.run==selectedRun && selectedRun==mission_host::run() && controller.health(enemy,fraction);
}
bool observe_enemy(const EnemyReceipt& receipt,bool death) noexcept {
    if(!mission_host::armed()) { return false; }
    const std::lock_guard lock(mutex);
    if(receipt.run!=selectedRun || receipt.run!=mission_host::run()) { return false; }
    const bool accepted=death?controller.died(receipt):controller.admitted(receipt);
    if(accepted) {
        std::array<char,256> line{};
        std::snprintf(line.data(),line.size(),"ev=strike_pact stage=%s run=%llu registry=%08X source=%u actor=%08X owner=%08X generation=%u evidence=native_%s",
            death?"death":"admission",static_cast<unsigned long long>(receipt.run),receipt.registry,receipt.source,receipt.actor,receipt.owner,receipt.generation,death?"health_death":"actor_creation");log(line.data());
    }
    return accepted;
}
bool observe_admission(const EnemyReceipt& receipt) noexcept { return observe_enemy(receipt,false); }
bool observe_death(const EnemyReceipt& receipt) noexcept { return observe_enemy(receipt,true); }
void observe_costs(std::uint32_t registry,std::uint16_t slot,const TaskCosts& report) noexcept {
    if(!mission_host::armed()) { return; }
    const auto run=mission_host::run();
    const std::lock_guard lock(mutex);
    if(run!=selectedRun) { return; }
    std::int8_t selected{-1};
    std::uint32_t known{};
    if(!controller.costed(registry,slot,report,selected,known)) { return; }
    std::array<char,320> line{};
    std::snprintf(line.data(),line.size(),"ev=strike_pact stage=task_assigned run=%llu registry=%08X type=1 slot=%u group=%d delta=%06X known=%06X evidence=native_task_costs",
        static_cast<unsigned long long>(run),registry,slot,static_cast<int>(selected),
        report.mask&0xFFFFFFU,known&0xFFFFFFU);
    log(line.data());
}
void observe_player_trigger(std::uint64_t run,std::uint32_t registry,std::uint16_t slot) noexcept {
    const std::lock_guard lock(mutex);
    if(run!=selectedRun || run!=mission_host::run()) { return; }
    if(controller.player_trigger(run,registry,slot)) {
        std::array<char,224> line{};
        std::snprintf(line.data(),line.size(),"ev=strike_pact stage=player_trigger run=%llu registry=%08X type=31 slot=%u evidence=incident_6685",
            static_cast<unsigned long long>(run),registry,slot);log(line.data());
    }
}
void observe_scene(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::scene_sense::Output& report) noexcept {
    const std::lock_guard lock(mutex);
    if(run==selectedRun && run==mission_host::run()) { controller.scene(run,registry,slot,report); }
}
void observe_squad(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::squad_sense::Output& report) noexcept {
    const std::lock_guard lock(mutex);
    if(run==selectedRun && run==mission_host::run()) { controller.squad(run,registry,slot,report); }
}
void observe_generator(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
                       std::uint32_t seed,std::uint32_t completed) noexcept {
    if(run!=mission_host::run()) { return; }
    const std::lock_guard lock(mutex);
    if(run==selectedRun) { controller.generator(run,registry,slot,seed,completed); }
}
void observe_monitor(std::uint32_t registry,std::uint16_t slot,bool any,
                     std::int32_t count,std::int32_t value) noexcept {
    if(!mission_host::armed()) { return; }
    const auto run=mission_host::run();const std::lock_guard lock(mutex);
    if(run!=selectedRun || !controller.monitor(run,registry,slot,any,count,value)) { return; }
    std::array<char,256> line{};
    std::snprintf(line.data(),line.size(),"ev=strike_pact stage=monitor_entered run=%llu registry=%08X type=30 slot=%u count=%d evidence=native_sense",
        static_cast<unsigned long long>(run),registry,slot,count);log(line.data());
}
void observe_combatant(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::combatant_sense::Output& report) noexcept {
    if(!mission_host::armed()
        || run!=mission_host::run()) { return; }
    const std::lock_guard lock(mutex);
    if(run!=selectedRun || !controller.combatant(run,registry,slot,report)) { return; }
    std::array<char,384> line{};
    std::snprintf(line.data(),line.size(),
        "ev=strike_pact stage=actor_feedback run=%llu registry=%08X type=2 slot=%u spawn=%u/%u program=%u/%u progress=%u/%u delivery=%u/%u delivery_state=%d detached=%u valid=%u",
        static_cast<unsigned long long>(run),registry,slot,report.spawnRevision,report.hasSpawnRevision,
        report.programRevision,report.hasProgramRevision,report.programState,report.hasProgramState,
        report.deliveryRevision,report.hasDeliveryRevision,report.deliveryState,report.detached,report.snapshotValid);
    log(line.data());
}
void observe_submission(std::uint64_t run,std::uint32_t definition,std::int64_t offset,
    std::uint32_t bank,std::uint8_t row,std::uint32_t generation) noexcept {
    if(offset!=0x1408 || row>=32 || run!=mission_host::run() || !mission_host::armed()) { return; }
    const std::lock_guard lock(mutex);
    if(run!=selectedRun || bank!=kBank || definition!=(controller.frame().campaign?0x80F47507U:0x80F55226U)) { return; }
    if(!loggedBank) {
        loggedBank=true;std::array<char,240> line{};
        std::snprintf(line.data(),line.size(),"ev=strike_pact stage=dialogue_component run=%llu definition=%08X bank=%08X evidence=native_dispatch",
            static_cast<unsigned long long>(run),definition,bank);log(line.data());
    }
    if(!controller.submitted(run,bank,row,generation,GetTickCount64())) { return; }
    std::array<char,240> line{};
    std::snprintf(line.data(),line.size(),"ev=strike_pact stage=dialogue_submitted run=%llu bank=%08X row=%u generation=%u evidence=native_dispatch",
        static_cast<unsigned long long>(run),bank,row,generation);log(line.data());
}
coo::CampaignScanRequest scan_request() noexcept {
    const std::lock_guard lock(mutex);return selectedRun==mission_host::run() && mission_host::armed()?controller.scan_request():coo::CampaignScanRequest{};
}
void observe_scan(coo::Generation owner,std::uint32_t handle,std::uint32_t serial,coo::ScanPlayback playback,bool participant) noexcept {
    const std::lock_guard lock(mutex);if(owner.run==selectedRun && owner.run==mission_host::run() && controller.scan_observation(owner,handle,serial,playback,participant)) log(controller.frame().scan.complete?"ev=campaign_scan mission=mission_pact stage=complete":"ev=campaign_scan mission=mission_pact stage=started");
}
} // namespace dawn::state::activity::strike_pact
