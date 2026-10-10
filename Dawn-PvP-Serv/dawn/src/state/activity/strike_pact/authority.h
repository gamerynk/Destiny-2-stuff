#pragma once
#include "player_triggers.h"
#include "../coo/native_player_trigger.h"
#include "frame.h"
#include "catalog_all.h"
#include "bindings.h"
#include "../coo/native_presentation_authority.h"
#include "../coo/native_combatant_authority.h"
#include "../coo/native_device_authority.h"
#include "../coo/native_generator_authority.h"
#include "../coo/native_scene_authority.h"
#include "../coo/native_scene_cast_authority.h"
#include "../coo/native_attachment_authority.h"
#include "../coo/native_clock_authority.h"
#include "../coo/native_music_authority.h"
namespace dawn::state::activity::strike_pact {
inline std::uint32_t encounter_key(const Frame& f,std::uint32_t key) noexcept {
    if(f.campaign && key==0x0F0A7E94U) return kRoot;
    if(f.campaign && key==0x4786C0E0U) return 0xF29221F5U;
    return key;
}

struct DeviceRow final { std::uint32_t registry; std::uint16_t slot; std::uint32_t bit; bool inverted; };
inline constexpr std::array<DeviceRow,2> kDevices{{
    {kOpening,kShieldWall,kDeviceGatewayShield,false},
    {kForest,kForestShieldWall,kDeviceForestShield,false},
}};
[[nodiscard]] constexpr const DeviceRow* device_row(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& row:kDevices) { if(row.registry==registry && row.slot==slot) { return &row; } }
    return nullptr;
}

[[nodiscard]] constexpr bool cohort_enabled(const Frame& frame,std::uint8_t cohort) noexcept {
    return cohort!=0 && cohort<64 && (frame.cohorts&(std::uint64_t{1}<<cohort))!=0;
}

[[nodiscard]] inline std::size_t body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,
                                           std::uint16_t slot) noexcept {
    key=encounter_key(frame,key);
    if(!frame.enabled || frame.spawnGeneration==0) { return 0; }
    if(frame.campaign && key==0x547F6321U && type==65 && slot==0) return 65;
    if(type==31 && player_trigger(key,slot)) { return coo::native_player_trigger::kAuthBits; }
    if(type==18 && key==0xF29221F5U && slot==2) { return 386; }
    for(const auto& region:presentation::kRegions) {
        if(key==region.registry && type==70 && slot==region.audience) { return 23; }
    }
    if(key==kBoss && frame.boss.prepared) {
        if(type==24 && live_laser_channel(slot)) {
            const auto room=slot<199?0U:slot<209?1U:2U;
            return frame.boss.laserObjects&(1U<<room)?99U:0U;
        }
        if(type==4 && slot>=87 && slot<=119) {
            const auto room=slot<96?0U:slot<105?1U:2U;
            return frame.boss.laserObjects&(1U<<room)?252U:0U;
        }
        if(type==43 && slot==kSceneMinotaur && frame.boss.sceneGeneration) { return 74; }
        if(type==26 && slot==kHopOnInvincible) { return frame.boss.immune?273:186; }
        if(type==23 && slot>=120 && slot<=125) { return 147; }
        if(type==30 && (slot==178 || slot==179)) { return 87; }
        if(type==4 && slot==kBossChest) { return 252; }
    }
    if(type==30 && key==kLedge && slot==kLedgeSensors[1].slot
        && cohort_enabled(frame,ledge_cohort(kCohortLedgeArrival))) {
        return 87U;
    }
    if(key==kRoot) {
        if(type==11 && slot==1) { return frame.musicCandidate<128?7223U:0U; }
        if(type==53 && slot==2) {
            return coo::native_presentation::dialogue_bits(frame.generations,frame.activeRow);
        }
        if(type==68 && slot==0 && frame.presentation.published) {
            return coo::native_presentation::kDirectiveBits;
        }
        return 0;
    }
    if(type==1) {
        const auto* source=all_spawn(key,slot);
        if(!source || !cohort_enabled(frame,source->cohort)) { return 0; }
        return coo::native_combatant::authored_source_bits(source->categories,task_objective(*source)!=nullptr);
    }
    if(type==2) {
        if(key==kHarvesterAsset.registry && slot==kHarvesterAsset.slot) {
            return frame.harvester.bits();
        }
        if(key==kOpening && slot==kGladiatorSensor) {
            return cohort_enabled(frame,opening_cohort(3))?coo::native_combatant::kBindBits:0;
        }
        if(key==kBoss && slot==kBossActor) {
            return cohort_enabled(frame,boss_cohort(kCohortParticipants))?coo::native_combatant::kBindBits:0;
        }
        return 0;
    }
    if(type==23) {
        const auto* row=device_row(key,slot);
        return row && (frame.devices&row->bit)!=0?147U:0U;
    }
    if(type==coo::native_generator::kSlotType && key==kForest && slot==kMapGenerator) {
        return frame.generatorSeed!=0?coo::native_generator::kActivationBits:0U;
    }
    if(type==4) {
        if(key==kTeleport && slot==0) { return frame.portalActive?252U:0U; }
        if(key==kChase && (frame.devices&kDeviceChaseLasers)!=0) {
            for(const auto laser:kChaseLasers) { if(laser==slot) { return 252U; } }
        }
        return 0;
    }
    return 0;
}

template<class Writer>
bool write_body(Writer& writer,const Frame& frame,std::uint32_t key,std::uint8_t type,
                std::uint16_t slot) noexcept {
    key=encounter_key(frame,key);
    if(body_bits(frame,key,type,slot)==0) { return false; }
    if(type==65) return frame.campaign && writer.write(0x80000000U+frame.spawnGeneration+1U,32)
        && writer.write(frame.scan.armed && !frame.scan.complete && !frame.finished?1U:0U,1) && writer.write(0x811C9DC5U,32);
    if(type==31) { return coo::native_player_trigger::arm(writer,frame.spawnGeneration); }
    if(type==18) { return coo::native_clock::countdown(writer,frame.completion.valid()
        && frame.completion.state==6,frame.endEpoch,frame.campaign?10000U:30000U); }
    if(type==70) {
        return writer.write(0,5) && writer.write(0,1) && writer.write(32769U,16) && writer.write(0,1);
    }
    if(key==kBoss && frame.boss.prepared) {
        if(type==24) {
            const auto room=slot<199?0U:slot<209?1U:2U;
            const std::array<float,1> value{frame.boss.laserHighRooms&(1U<<room)?100.F:0.F};
            return coo::native_atom::write_channels(writer,value,static_cast<std::int32_t>(frame.boss.laserRevision));
        }
        if(type==43) { return coo::native_scene::cast_scene(writer,frame.boss.sceneGeneration,{},{},0); }
        if(type==26) { return coo::native_attachment::squad(writer,kBoss,kBossSquad,frame.boss.immune); }
        if(type==23 && slot>=120 && slot<=125) {
            const auto room=(slot-120)/2;
            const bool mapAccess=frame.campaign && frame.scan.armed && slot==kBossRooms.back().exit;
            const bool open=(slot%2)==0 || (frame.boss.cleared&(1U<<room))!=0 || mapAccess;
            return coo::native_device::position_only(writer,open?0.F:1.F,
                static_cast<std::int16_t>(frame.spawnGeneration+(open?1U:0U)),(slot%2)==0);
        }
        if(type==4 && slot==kBossChest) {
            return coo::native_device::object(writer,frame.spawnGeneration,frame.boss.dead);
        }
    }
    if(type==30) {
        return writer.write(0x811C9DC5U,32) && writer.write(0,7)
            && writer.write(32767U,16)
            && writer.write(static_cast<std::uint32_t>(kLedgeMonitorOccupancyValue)+0x80000000U,32);
    }
    if(key==kRoot) {
        if(type==11) { return coo::native_music::select(writer,frame.musicCandidate); }
        if(type==53) { return coo::native_presentation::dialogue(writer,frame.generations,frame.activeRow); }
        const auto* region=presentation::region(frame.region);
        const coo::Asset audience=region?coo::Asset{region->registry,region->tag,70,region->audience}:coo::Asset{};
        return coo::native_presentation::objective(writer,frame.presentation,audience,true);
    }
    if(type==1) {
        const auto index=all_spawn_index(key,slot);
        const auto& source=kAllSpawns[index];
        coo::native_combatant::Source body{key,frame.spawnGeneration,0,source.loose,{},source.second,
                                           source.categories==2,false};
        body.variant = frame.enemyVariant==5 && grandmaster_substitution_source(key,slot) ? 5U : 0U;
        body.sceneRequested=key==kLedge && reserved_cargo(slot);
        if(const auto* objective=task_objective(source)) {
            body.tactical={objective->registry,objective->slot,
                static_cast<std::int8_t>(static_cast<int>(frame.taskPlusOne[index])-1),
                objective->revision};
        }
        // shared authored actor profile {0,0,0,0}; do not turn the last three fields into -1
        return coo::native_combatant::write_authored_source(writer,body,{0,0,0,0});
    }
    if(type==2) {
        if(key==kHarvesterAsset.registry && slot==kHarvesterAsset.slot) {
            return frame.harvester.write(writer);
        }
        if(key==kBoss && slot==kBossActor) { return coo::native_combatant::write_spawn(writer,frame.spawnGeneration); }
        return coo::native_combatant::write_bind(writer,frame.spawnGeneration);
    }
    if(type==coo::native_generator::kSlotType) {
        return coo::native_generator::write_activation(writer,
            coo::native_generator::forest_request(frame.generatorSeed));
    }
    if(type==23) {
        const auto* row=device_row(key,slot);
        const bool removed=((frame.devices&row->bit)!=0) && ((frame.devicesOpen&row->bit)!=0);
        return coo::native_device::position_only(writer,removed==row->inverted?1.F:0.F,
            static_cast<std::int16_t>(frame.spawnGeneration+(removed?1U:0U)),true);
    }
    if(type==4) { return coo::native_device::object(writer,frame.spawnGeneration,true); }
    return false;
}
} // namespace dawn::state::activity::strike_pact
