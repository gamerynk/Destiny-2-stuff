#include "controller.h"
#include "authority.h"
#include <algorithm>
#include <chrono>
namespace dawn::state::activity::strike_pact {
void Controller::generator(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
                           std::uint32_t seed,std::uint32_t completed) noexcept {
    if(run==run_ && region_==kForestRegion && registry==kForest && slot==kMapGenerator
        && frame_.generatorSeed && seed==frame_.generatorSeed && completed
        && !frame_.firstForestAreaComplete) {
        frame_.firstForestAreaComplete=true;++frame_.revision;
    }
}
bool valid_document(const coo::script::Views& views) noexcept {
    if(!views.valid || (views.missionId!="strike_pact" && views.missionId!="mission_pact") || views.profileId!=kProfile.id || views.phases.empty()
        || views.mission.modules.size()!=1 || views.mission.modules[0].asset!=kModule
        || views.mission.modules[0].id!=1) { return false; }
    return coo::script::authorized(views,kProfile);
}

void Controller::reset() noexcept {
    executor_.cancel(*this); composition_.reset(); views_=nullptr;run_=0;now_=0;
    started_=false;landingSeen_=false;dialogueSubmitted_.reset();dialogue_={};frame_={};seen_.reset();
    population_={};objectives_={};lifecycle_.reset();voiceEnds_={};costs_={};harvester_={};
    bossScene_={};bossActor_={};bossEnemy_={};bossSeen_=bossRemoved_=bossEnded_=false;bossAlive_=0;
    laserDeadline_=0;laserHigh_=false;clock_.reset();
    ledgeFinalSeen_=false;region_=-1;
}
bool Controller::select(const coo::script::Views& views,std::uint64_t run) noexcept {
    if(run==0 || !valid_document(views)) { reset();return false; }
    if(run_==run) { return views_==&views; }
    reset();
    if(!lifecycle_.begin(run)) { return false; }
    publicationGeneration_=lifecycle_.owner().value;views_=&views;run_=run;frame_.spawnGeneration=publicationGeneration_;
    frame_.campaign=views.missionId=="mission_pact";frame_.services=true;return true;
}
void Controller::position(std::uint64_t run,Point point) noexcept {
    if(run!=run_ || !views_) { return; }
    if(!views_->observationStart) { landingSeen_=true; }
    for(std::size_t i=0;i<kAllVolumes.size() && !landingSeen_;++i) {
        const auto& asset=views_->observationStart->asset;
        if(volume_region(i)==region_ && kAllVolumes[i].registry==asset.registry
            && kAllVolumes[i].slot==asset.slot && contains(kAllVolumes[i],point)) { landingSeen_=true; }
    }
    if(!landingSeen_) { return; }
    for(std::size_t i=0;i<kAllVolumes.size();++i) {
        if(volume_region(i)==region_ && contains(kAllVolumes[i],point)) { seen_.set(i); }
    }
}
bool Controller::entered(coo::Asset asset) const noexcept {
    if(asset==coo::Asset{kForest,kForestTag,37,kMapGenerator}) { return frame_.firstForestAreaComplete; }
    if(asset.type==30) {
        return asset.registry==kLedge && asset.slot==kLedgeSensors[1].slot && ledgeFinalSeen_;
    }
    if(asset.type!=60) { return false; }
    for(std::size_t i=0;i<kAllVolumes.size();++i) {
        if(kAllVolumes[i].registry==asset.registry && kAllVolumes[i].slot==asset.slot) { return seen_[i]; }
    }
    return false;
}
bool Controller::player_trigger(std::uint64_t run,std::uint32_t registry,std::uint16_t slot) noexcept {
    if(run!=run_ || !views_ || !frame_.enabled) { return false; }
    const auto* trigger=strike_pact::player_trigger(registry,slot);
    if(!trigger) { return false; }
    for(std::size_t i=0;i<kAllVolumes.size();++i) {
        if(kAllVolumes[i].registry!=registry || kAllVolumes[i].slot!=trigger->volume || seen_[i]) { continue; }
        seen_.set(i);
        if(!views_->observationStart || (views_->observationStart->asset.registry==registry
            && views_->observationStart->asset.slot==trigger->volume)) { landingSeen_=true; }
        ++frame_.revision;return true;
    }return false;
}
bool Controller::monitor(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
                         bool any,std::int32_t count,std::int32_t value) noexcept {
    if(run==run_ && views_ && frame_.enabled && region_==kBossRegion && registry==kBoss
        && frame_.boss.prepared && any && count>0 && value==kLedgeMonitorOccupancyValue) {
        for(const auto& room:kBossRooms) {
            if(room.index>1 && slot==room.monitor) {
                frame_.boss.arrived|=static_cast<std::uint8_t>(1U<<(room.index-1));
                ++frame_.revision;return true;
            }
        }
    }
    if(run!=run_ || !views_ || !frame_.enabled || region_!=0
        || !cohort_enabled(frame_,ledge_cohort(kCohortLedgeArrival))
        || registry!=kLedge || slot!=kLedgeSensors[1].slot || !any || count<=0
        || value!=kLedgeMonitorOccupancyValue || ledgeFinalSeen_) { return false; }
    ledgeFinalSeen_=true;++frame_.revision;return true;
}
bool Controller::submitted(std::uint64_t run,std::uint32_t bank,std::uint8_t row,std::uint32_t generation,std::uint64_t now) noexcept {
    if(!views_ || run!=run_ || !started_ || row>=32) { return false; }
    if(bank!=kBank) return false;
    const auto& policy=views_->dialogue;
    if(!dialogue_.submitted(policy,bank,row,generation,now,frame_,frame_.revision)) { return false; }
    dialogueSubmitted_.set(row);voiceEnds_[row]=dialogue_.voice_until();
    return true;
}
bool Controller::publish(const coo::Command& command) noexcept {
    if(!views_ || !graph() || !coo::script::valid_token(graph()->definition,executor_,command)) { return false; }
    const auto& spec=command.spec;
    if(spec.operation==coo::Operation::objective) {
        dialogue_.objective(views_->dialogue,spec.argument,frame_,frame_.revision);
        coo::MarkerTarget marker{};for(const auto& binding:views_->markers) { if(binding.event==spec.argument) { marker=binding.target; } }
        objectives_.set(spec.argument,marker);++frame_.revision;
    } else if(spec.operation==coo::Operation::dialogue) {
        dialogue_.enqueue(views_->dialogue,static_cast<std::uint8_t>(spec.argument),now_,0,0,frame_.revision);
    } else if(spec.operation==coo::Operation::population) { enable(spec.argument);++frame_.revision; }
    else if(spec.operation==coo::Operation::mechanic) {
        if(spec.asset==kBossMechanic) { return boss_command(command); }
        if(spec.asset==kHarvesterAsset) {
            using namespace coo::native_atom;
            if(spec.argument==kMechanicShipArrival) {
                auto program=thresher_arrival();program.generation=frame_.spawnGeneration;
                if(!harvester_.program(command.token,program)) { return false; }
                enable(ledge_cohort(kCohortHarvester));
                enable(ledge_cohort(kCohortHarvesterCargo));
            } else if(spec.argument==kMechanicShipDelivery) {
                std::array<Ref,kHarvesterCargoSources.size()> cargo{};
                for(std::size_t i=0;i<cargo.size();++i) {
                    cargo[i]={kLedge,1,static_cast<std::int16_t>(kHarvesterCargoSources[i])};
                }
                if(!harvester_.deliver(command.token,cargo,kArrivalRevision)) { return false; }
                enable(ledge_cohort(kCohortLedgeReinforcements));
            } else if(spec.argument==kMechanicShipDeparture) {
                auto program=thresher_departure();program.generation=frame_.spawnGeneration;
                if(!harvester_.program(command.token,program)) { return false; }
            } else if(spec.argument==kMechanicShipRetire) {
                if(!harvester_.retire(command.token)) { return false; }
            } else { return false; }
            frame_.harvester=harvester_.publication();++frame_.revision;
        } else if(spec.asset==kObjectiveAsset && spec.argument==kMechanicMarkerClear) {
            objectives_.clear_marker();++frame_.revision;
        } else if(spec.asset==kModule && spec.argument==kMechanicModule) {
        } else if(spec.argument==kMechanicGenerator) {
            // activating the Infinite Forest worker is what places its encounters
            frame_.generatorSeed=coo::native_generator::mission_seed(
                static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()
                    ^ (run_*2654435761ULL)));
            ++frame_.revision;
        } else if(spec.asset==kMissionAsset && spec.argument==80) {
            if(!frame_.campaign || !frame_.boss.dead) return false;
            frame_.scan.armed=true;++frame_.revision;
        } else if(spec.asset==kMissionAsset && spec.argument>=kMechanicCheckpointBase) {
            const auto index=spec.argument-kMechanicCheckpointBase;
            if(index>=std::size(presentation::kCheckpoints)) { return false; }
            const auto& point=presentation::kCheckpoints[index];
            frame_.checkpointSliceSet=point.region;
            frame_.checkpointSpawnSet=point.spawnSet;
            ++frame_.revision;
        } else { return false; }
    }
    else if(spec.operation==coo::Operation::complete) {
        if(!lifecycle_.complete_timed(lifecycle_.owner(),now_,frame_.campaign?10000U:30000U)) { return false; }
        frame_.endEpoch=frame_.activityTime;frame_.musicCandidate=16;
        objectives_.clear();frame_.finished=true;++frame_.revision;
    }
    else if(spec.operation==coo::Operation::device) {
        if(spec.asset==kPortalAsset) { frame_.portalActive=true;++frame_.revision; }
        else if(const auto* row=device_row(spec.asset.registry,spec.asset.slot)) {
            // take and release are the same command; position 1 is the device's authored presence, 0 removes it
            frame_.devices|=row->bit;
            if(spec.argument==0) { frame_.devicesOpen|=row->bit; }
            ++frame_.revision;
        } else if(spec.asset.registry==kChase && spec.asset.type==4) {
            frame_.devices|=kDeviceChaseLasers;++frame_.revision;
        } else { return false; }
    }
    return true;
}
void Controller::enable(std::uint32_t cohort) noexcept {
    if(cohort==0 || cohort>kAllLastCohort) { return; }
    // a later clearance command must not reset the task selection or cost feedback already established
    if(cohort_enabled(frame_,static_cast<std::uint8_t>(cohort))) { return; }
    if(cohort==boss_cohort(kCohortPrefight)) { frame_.boss.prepared=true; }
    frame_.cohorts|=std::uint64_t{1}<<cohort;
    for(std::size_t i=0;i<kAllSpawns.size();++i) { if(kAllSpawns[i].cohort==cohort) {
        frame_.taskPlusOne[i]=static_cast<std::uint8_t>(task_initial(kAllSpawns[i])+1);
        costs_[i]={};
        const auto task=tactical_group(kAllSpawns[i]);
        // only explicitly tasked sources verify their tactical assignment; the rest keep the package's own selection
        population_.policy(i,{true,task.row>=0?coo::EnemyIntent::combat:coo::EnemyIntent::idleReveal,task.registry,task.slot,task.row});
        population_.enable(i);
    } }
}
// live population is readiness and a consumed request is clear. The shared probe also wants a bound
// health component, which is never observed on these actors, so no clear could ever be accepted
bool Controller::ready(std::uint32_t cohort) const noexcept {
    if(cohort==0 || cohort>kAllLastCohort || frame_.populationFault) { return false; }
    for(std::size_t i=0;i<kAllSpawns.size();++i) {
        if(kAllSpawns[i].cohort==cohort && kAllSpawns[i].required
            && !population_.admitted(i,expected_actors(kAllSpawns[i]))) { return false; }
    }
    return true;
}
bool Controller::cleared(std::uint32_t cohort) const noexcept {
    if(cohort==0 || cohort>kAllLastCohort || frame_.populationFault) { return false; }
    for(std::size_t i=0;i<kAllSpawns.size();++i) {
        if(kAllSpawns[i].cohort==cohort && kAllSpawns[i].required && !population_.cleared(i,expected_actors(kAllSpawns[i]))) { return false; }
    }
    return true;
}
bool Controller::admitted(const EnemyReceipt& receipt) noexcept {
    if(!views_ || !frame_.enabled || frame_.populationFault) { return false; }
    // the reserved boss squad has no loose actors; its named member supplies the admission receipt
    if(receipt.valid() && receipt.run==run_ && receipt.generation==publicationGeneration_
        && receipt.registry==kBoss && receipt.source==kBossSquad && frame_.boss.sceneGeneration) {
        if(bossEnemy_.valid()) { return bossEnemy_==receipt; }
        bossEnemy_=receipt;++frame_.revision;return true;
    }
    const auto result=population_.admit(kAllSpawns,receipt,run_,publicationGeneration_);
    if(result==coo::Admission::overflow) { frame_.populationFault=true; }
    if(result==coo::Admission::accepted && receipt.registry==kBoss && receipt.source==kMinotaurSquad) {
        frame_.boss.minotaurSpawned=true;++frame_.revision;
    }
    return result==coo::Admission::accepted;
}
bool Controller::costed(std::uint32_t registry,std::uint16_t slot,const TaskCosts& report,
                        std::int8_t& selected,std::uint32_t& known) noexcept {
    selected=-1;known=0;
    if(!views_ || !frame_.enabled || frame_.populationFault) { return false; }
    const auto index=all_spawn_index(registry,slot);
    if(index>=kAllSpawns.size()) { return false; }
    const auto& source=kAllSpawns[index];
    const auto current=static_cast<int>(frame_.taskPlusOne[index])-1;
    selected=static_cast<std::int8_t>(current);
    // task changes keep spawn generation and request counts and do not request another delivery
    if(!cohort_enabled(frame_,source.cohort) || task_fixed(source)) { return false; }
    auto& retained=costs_[index];
    retained.merge(report);
    known=retained.mask;
    const auto* objective=task_objective(source);
    if(!objective || !retained.hasRevision || retained.revision!=objective->revision) { return false; }
    int best=-1;
    std::uint8_t bestCost=kTaskUnreachable;
    for(std::uint8_t group=0;group<objective->groups;++group) {
        if(!task_allowed(source,group) || (retained.mask&(std::uint32_t{1}<<group))==0) { continue; }
        const auto cost=retained.cost[group];
        if(cost>=bestCost) { continue; }
        bestCost=cost;best=group;
    }
    const auto reported=[&](int group) noexcept {
        return group>=0 && task_allowed(source,static_cast<std::uint8_t>(group))
            && (retained.mask&(std::uint32_t{1}<<group))!=0;
    };
    // hold a group that only ties the cheapest, so a squad does not flip between equal routes
    int next=best;
    if(best>=0 && reported(current) && retained.cost[current]==bestCost) { next=current; }
    if(best<0) { next=task_allowed(source,static_cast<std::uint8_t>((std::max)(current,0)))&&current>=0?current:-1; }
    selected=static_cast<std::int8_t>(next);
    if(next==current) { return false; }
    frame_.taskPlusOne[index]=static_cast<std::uint8_t>(next+1);
    ++frame_.revision;
    return true;
}
bool Controller::died(const EnemyReceipt& receipt) noexcept {
    if(!views_ || !frame_.enabled || !population_.died(receipt,run_,publicationGeneration_)) { return false; }
    if(receipt.registry==kBoss && receipt.source==kMinotaurSquad) { frame_.boss.minotaurDead=true; }
    ++frame_.revision;return true;
}
bool Controller::combatant(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::combatant_sense::Output& report) noexcept {
    if(run==run_ && views_ && frame_.enabled && region_==kBossRegion
        && registry==kBoss && slot==kBossActor && frame_.boss.sceneGeneration) {
        boss_health(report);++frame_.revision;return true;
    }
    if(run!=run_ || !views_ || !frame_.enabled || region_!=0
        || registry!=kHarvesterAsset.registry || slot!=kHarvesterAsset.slot) { return false; }
    const auto before=harvester_.state();
    harvester_.observe(run,report);
    if(before==harvester_.state()) { return false; }
    ++frame_.revision;return true;
}
void Controller::project_services() noexcept {
    frame_.presentation=objectives_.state();frame_.completion=lifecycle_.publication();
    auto& objective=frame_.presentation;
    if(objective.active && frame_.region==72 && objective.event==kTraverseForest) {
        objective.marker=presentation::marker(presentation::kNavPoints[1]);
    }
    if(objective.active && frame_.region==0 && objective.event==kFindLeader) {
        objective.marker=presentation::marker(presentation::kNavPoints[5]);
    }
}
bool Controller::observed(const coo::CommandSpec& spec) const noexcept {
    if(spec.asset==kMissionAsset && spec.argument==81) return frame_.scan.started;
    if(spec.asset==kMissionAsset && spec.argument==82) return frame_.scan.complete;
    if(views_ && views_->condition(spec)) { return views_->evaluate(spec,[this](const auto& native) noexcept { return observed(native); }); }
    if(spec.asset==kDialogueAsset) { return spec.argument<32 && dialogueSubmitted_[spec.argument] && now_>=voiceEnds_[spec.argument]; }
    if(spec.asset==kModule) { return cleared(spec.argument); }
    if(spec.asset==kBossMechanic) { return boss_observed(spec.argument); }
    // a held region comes from a portal or fade; no volume can stand for it
    if(spec.asset==kRegionAsset) { return region_==static_cast<int>(spec.argument); }
    return entered(spec.asset);
}
coo::StallDetail Controller::missing(const coo::CommandSpec& spec) const noexcept {
    using coo::Missing;
    if(spec.wait==coo::Wait::requested || (coo::is_observation(spec.operation) && observed(spec))) { return {}; }
    if(views_ && views_->condition(spec)) { return {Missing::observation,spec.asset}; }
    if(spec.operation==coo::Operation::population) {
        for(std::size_t i=0;i<kAllSpawns.size();++i) {
            const auto& source=kAllSpawns[i];if(source.cohort!=spec.argument || !source.required) { continue; }
            auto detail=population_.missing(i,expected_actors(source),true);
            if(detail.missing!=Missing::none) { detail.asset={source.registry,kOpeningTag,1,source.source};return detail; }
        }return {};
    }
    if(spec.operation==coo::Operation::dialogue) {
        return spec.argument<32 && dialogueSubmitted_[spec.argument]?coo::StallDetail{}:coo::StallDetail{Missing::dialogue,spec.asset,spec.argument};
    }
    return {entered(spec.asset)?Missing::none:Missing::observation,spec.asset};
}
void Controller::update_module(std::uint32_t id,const coo::MissionInput& input,Frame& output) noexcept {
    if(id!=kMechanicModule || !views_ || input.run!=run_) { return; }
    now_=input.now;region_=input.region;
    if(region_>=0) { frame_.region=static_cast<std::uint16_t>(region_); }
    // route dialogue is an optional cue, never a prerequisite for leaving a region
    if(const auto* route=views_->table(frame_.campaign?"campaign_route":"route")) {
        for(const auto& binding:route->dialogue) {
            if(entered(binding.asset)) {
                dialogue_.enqueue(views_->dialogue,binding.row,now_,binding.delayMs,0,frame_.revision);
            }
        }
    }
    const auto candidate=region_==72?2U:region_==16?5U:region_==0?8U:255U;
    if(candidate<128 && (frame_.musicCandidate==255 || candidate>frame_.musicCandidate)) {
        frame_.musicCandidate=static_cast<std::uint8_t>(candidate);++frame_.revision;
    }
    frame_.activityTime=clock_.sample(now_);
    if(lifecycle_.advance(now_)) { ++frame_.revision; }
    advance_boss();
    project_services();
    const auto& currentGraph=*graph();
    if(!started_) { started_=executor_.start(currentGraph.definition,run_); }
    if(!started_) { return; }
    executor_.update(*this);
    for(const auto& binding:currentGraph.commands) {
        const auto& spec=currentGraph.definition.steps[binding.step].commands[binding.command];
        const auto state=executor_.step_state(binding.step);
        if(state.phase!=coo::StepPhase::active || !state.commands[binding.command].requested) { continue; }
        const coo::Token token{run_,executor_.diagnostics().incarnation,binding.step,binding.command};
        if(coo::is_observation(spec.operation) && observed(spec)) {
            static_cast<void>(executor_.enqueue({token,coo::Milestone::observed}));
        } else if(spec.operation==coo::Operation::mechanic && spec.asset==kBossMechanic
            && spec.argument==kBossBegin && frame_.boss.sceneFinished
            && bossScene_.owner(0).token==token) {
            static_cast<void>(executor_.enqueue({token,coo::Milestone::nativeReady}));
            static_cast<void>(executor_.enqueue({token,coo::Milestone::completed}));
        } else if(spec.operation==coo::Operation::mechanic && spec.asset==kHarvesterAsset
            && harvester_.owns(token)) {
            const auto& actorState=harvester_.state();
            const auto generation=frame_.spawnGeneration;
            bool completed=false;
            using namespace coo::native_atom;
            if(spec.argument==kMechanicShipArrival) {
                completed=actorState.program(generation,kArrivalRevision,kArrivalCompleteState);
            } else if(spec.argument==kMechanicShipDelivery) {
                completed=actorState.delivery(generation,kArrivalRevision,0)
                    && ready(ledge_cohort(kCohortHarvesterCargo));
            } else if(spec.argument==kMechanicShipDeparture) {
                completed=actorState.program(generation,kDepartureRevision,kDepartureCompleteState)
                    || harvester_.detached_after_last_atom(token);
            }
            if(completed) {
                static_cast<void>(executor_.enqueue({token,coo::Milestone::nativeReady}));
                static_cast<void>(executor_.enqueue({token,coo::Milestone::completed}));
            }
        } else if(spec.operation==coo::Operation::dialogue && spec.argument<32 && dialogueSubmitted_[spec.argument]) {
            static_cast<void>(executor_.enqueue({token,coo::Milestone::nativeReady}));
        } else if(spec.operation==coo::Operation::population && spec.wait==coo::Wait::completed) {
            if(ready(spec.argument)) { static_cast<void>(executor_.enqueue({token,coo::Milestone::nativeReady})); }
            if(cleared(spec.argument)) { static_cast<void>(executor_.enqueue({token,coo::Milestone::completed})); }
        }
        if(frame_.populationFault) { static_cast<void>(executor_.enqueue({token,coo::Milestone::failed})); }
    }
    executor_.update(*this);
    executor_.update(*this);
    dialogue_.advance(views_->dialogue,publicationGeneration_-1U,now_,false,frame_,frame_.revision);
    frame_.enabled=executor_.diagnostics().phase!=coo::Phase::failed;
    if(executor_.diagnostics().phase==coo::Phase::complete && std::size_t(frame_.section)+1<views_->phases.size()) {
        ++frame_.section;executor_.cancel(*this);
        started_=executor_.start(graph()->definition,run_);
        if(started_) { executor_.update(*this); }
    }
    frame_.checked=frame_.finished;
    project_services();output=frame_;
}
Frame Controller::update(std::uint64_t run,std::uint64_t now,bool ready,int region) noexcept {
    if(!views_ || run!=run_ || !ready) { return {}; }
    // every section after the opening is entered by portal or fade, so the held region is the only
    // evidence the player arrived
    return composition_.update(views_->mission,{run,now,region,false,true},*this);
}

bool Controller::boss_command(const coo::Command& command) noexcept {
    auto& boss=frame_.boss;
    if(command.spec.argument==kBossBegin) {
        if(boss.sceneGeneration || !boss.prepared || !cleared(boss_cohort(kCohortPrefight))) { return false; }
        if(!bossScene_.begin(0,frame_.spawnGeneration,SceneOwner{command.token})) { return false; }
        enable(boss_cohort(kCohortParticipants));
        boss.sceneGeneration=frame_.spawnGeneration;
        frame_.musicCandidate=10;
        boss.room=1;
        frame_.taskPlusOne[all_spawn_index(kBoss,kBossSquad)]=kBossRooms[0].arrivalGroup+1;
    } else {
        const auto room=command.spec.argument-kBossRoom1Start+1;
        if(room<1 || room>3 || !boss.sceneFinished || boss.dead) { return false; }
        const auto bit=static_cast<std::uint8_t>(1U<<(room-1));
        if(room>1 && ((boss.arrived&bit)==0 || (boss.cleared&(bit>>1))==0)) { return false; }
        boss.room=static_cast<std::uint8_t>(room);boss.immune=false;boss.fights|=bit;
        if(room>1) { frame_.musicCandidate=room==2?12:14; }
        enable(boss_cohort(static_cast<std::uint8_t>(kCohortRoom1+room-1)));
        boss_lasers(static_cast<std::uint8_t>(room),true);
        if(room>1) { frame_.checkpointSliceSet=kBossRegion;frame_.checkpointSpawnSet=kBossRooms[room-1].spawnSet; }
    }
    ++frame_.revision;return true;
}
void Controller::scene(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::scene_sense::Output& report) noexcept {
    if(run!=run_ || region_!=kBossRegion || registry!=kBoss || slot!=kSceneMinotaur
        || !bossScene_.requested(0) || !report.delta
        || report.generationWire!=0x80000000U+bossScene_.commands()[0].generation) { return; }
    if(report.completed && !frame_.boss.sceneFinished) {
        bossScene_.mark(0,1);frame_.boss.sceneFinished=true;++frame_.revision;
    }
}
void Controller::squad(std::uint64_t run,std::uint32_t registry,std::uint16_t slot,
    const middleware::bap::activity_message::squad_sense::Output& report) noexcept {
    if(run!=run_ || region_!=kBossRegion || registry!=kBoss || slot!=kBossSquad
        || !frame_.boss.sceneGeneration) { return; }
    if(!report.initialized) { bossAlive_=0;bossSeen_=bossRemoved_=false;return; }
    if(report.hasAlive) { bossAlive_=report.alive; }
    if(bossAlive_>0) { bossSeen_=true;bossRemoved_=false; }
    else if(bossSeen_ && report.removal) { bossRemoved_=true; }
}
void Controller::boss_health(const middleware::bap::activity_message::combatant_sense::Output& report) noexcept {
    const auto prior=bossActor_;
    const bool final=frame_.boss.sceneFinished && frame_.boss.room==3 && !frame_.boss.immune;
    if(!report.snapshotValid) { bossActor_={};bossEnded_=bossRemoved_=false;return; }
    if(report.detached) {
        if(final && prior.identified && prior.spawnRevision==frame_.spawnGeneration && prior.hasActorQuery) { bossEnded_=true; }
        bossActor_={};return;
    }
    bossActor_.merge(report);
    if(!bossActor_.identified || bossActor_.spawnRevision!=frame_.spawnGeneration) {
        bossEnded_=bossRemoved_=false;return;
    }
    if(bossActor_.hasActorQuery) { bossEnded_=final && bossActor_.actorQuery==0; }
}
BossRequest Controller::boss_request() const noexcept {
    const auto& boss=frame_.boss;
    if(!frame_.enabled || frame_.finished || boss.dead || !boss.sceneGeneration || !bossEnemy_.valid()
        || boss.room<1 || boss.room>3) {return {};}
    return {lifecycle_.owner(),bossEnemy_,static_cast<std::uint8_t>(boss.room-1),frame_.revision,
        !boss.sceneFinished || boss.immune || !(boss.fights&(1U<<(boss.room-1)))};
}
bool Controller::health(const EnemyReceipt& enemy,float fraction) noexcept {
    const auto request=boss_request();
    if(!request.owner.valid() || enemy!=request.enemy || !std::isfinite(fraction) || fraction<0 || fraction>1) {return false;}
    auto& boss=frame_.boss;
    if(boss.healthObserved && fraction>=boss.health) {return false;}
    boss.healthObserved=true;boss.health=fraction;++frame_.revision;
    advance_boss();return true;
}
void Controller::boss_lasers(std::uint8_t room,bool on) noexcept {
    auto& boss=frame_.boss;const auto bit=static_cast<std::uint8_t>(1U<<(room-1));
    if(on) {
        if(boss.laserRooms&bit) { return; }
        if(!boss.laserRooms) {
            laserHigh_=true;laserDeadline_=now_+kLaserHighMs;boss.laserHighRooms|=bit;
        }
        boss.laserRooms|=bit;boss.laserObjects|=bit;
    } else { boss.laserRooms&=~bit;boss.laserHighRooms&=~bit; }
    if(!boss.laserRooms) { laserDeadline_=0;laserHigh_=false; }
    ++boss.laserRevision;++frame_.revision;
}
void Controller::advance_boss() noexcept {
    auto& boss=frame_.boss;
    if(!boss.prepared) { return; }
    if(boss.sceneFinished && boss.room>0 && boss.room<3 && (boss.fights&(1U<<(boss.room-1))) && !boss.immune
        && (boss.healthObserved || (bossActor_.identified && bossActor_.spawnRevision==frame_.spawnGeneration && bossActor_.hasActorQuery))) {
        const auto bit=static_cast<std::uint8_t>(1U<<(boss.room-1));
        const float threshold=boss.room==1?kBossGateRoom1:kBossGateRoom2;
        // damage boundary gives the exact fraction; sense is a quantized fallback (a thirds floor may round to 85/127)
        const bool reached=boss.healthObserved ? boss.health<=threshold
            :bossActor_.actorQuery<=static_cast<std::uint32_t>(std::ceil(threshold*127.F));
        if(!(boss.retreated&bit) && reached) {
            boss.retreated|=bit;boss.immune=true;
            const auto& next=kBossRooms[boss.room];
            enable(boss_cohort(static_cast<std::uint8_t>(kCohortRoom1+boss.room)));
            frame_.taskPlusOne[all_spawn_index(kBoss,kBossSquad)]=next.arrivalGroup+1;
            boss_lasers(next.index,true);++frame_.revision;
        }
    }
    if(!boss.dead && boss.sceneFinished && boss.room==3 && !boss.immune && bossEnded_ && bossRemoved_) {
        boss.dead=true;
        for(std::uint8_t room=1;room<=3;++room) { boss_lasers(room,false); }
        ++frame_.revision;
    }
    for(std::uint8_t room=1;room<=3;++room) {
        const auto bit=static_cast<std::uint8_t>(1U<<(room-1));
        if((boss.cleared&bit)==0 && (room<3?(boss.retreated&bit)!=0:boss.dead)
            && cohort_enabled(frame_,boss_cohort(static_cast<std::uint8_t>(kCohortRoom1+room-1)))
            && cleared(boss_cohort(static_cast<std::uint8_t>(kCohortRoom1+room-1)))) {
            boss.cleared|=bit;if(room<3) { boss_lasers(room,false); }++frame_.revision;
        }
    }
    if(laserDeadline_ && now_>=laserDeadline_ && !boss.dead) {
        laserHigh_=!laserHigh_;boss.laserHighRooms=laserHigh_?boss.laserRooms:0;
        laserDeadline_=now_+(laserHigh_?kLaserHighMs:kLaserLowMs);
        ++boss.laserRevision;++frame_.revision;
    }
}
bool Controller::boss_observed(std::uint32_t argument) const noexcept {
    const auto& boss=frame_.boss;
    switch(argument) {
    case kBossRetreat2:return (boss.retreated&1)!=0;
    case kBossRetreat3:return (boss.retreated&2)!=0;
    case kBossRoom1Exit:return (boss.cleared&1)!=0;
    case kBossRoom2Exit:return (boss.cleared&2)!=0;
    case kBossRoom2Start:return (boss.arrived&2)!=0;
    case kBossRoom3Start:return (boss.arrived&4)!=0;
    case kBossMinotaurSpawned:return boss.minotaurSpawned || boss.sceneFinished;
    // the authored reveal kills its Minotaur, so its completion also proves the death if no death receipt arrives
    case kBossMinotaurDead:return boss.minotaurDead || boss.sceneFinished;
    case kBossDeath:return boss.dead;
    default:return false;
    }
}
} // namespace dawn::state::activity::strike_pact
