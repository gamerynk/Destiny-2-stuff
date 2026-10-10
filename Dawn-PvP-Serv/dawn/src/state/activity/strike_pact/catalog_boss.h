#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include "catalog.h"
#include "catalog_ledge.h"
#include "../coo/objective_service.h"
namespace dawn::state::activity::strike_pact {

inline constexpr std::uint32_t kBoss=0xA5F083B5U, kBossTag=0x80F54E07U;
inline constexpr std::uint32_t kBossPoints=0xC6F46FAFU, kBossPointsTag=0x80F54E1DU;
inline constexpr int kBossRegion=0;
inline constexpr std::uint8_t kBossBubble=0;
inline constexpr std::uint32_t kBossStateHash=0xDA8AE171U;

// music.select is monotonic, so these only ever climb
inline constexpr std::uint8_t kBossMusicIntro=10, kBossMusicRoom2=12, kBossMusicRoom3=14,
    kBossMusicDead=16;
inline constexpr std::uint8_t kNoBossCue=0xFFU;
inline constexpr std::uint16_t kNoBossMonitor=0xFFFFU;

inline constexpr std::uint16_t kBossObjectiveSlot=1;
inline constexpr std::uint8_t kBossObjectiveType=3, kBossObjectiveGroups=17, kBossObjectiveRevision=1;

inline constexpr std::uint16_t kBossAudience=176;
inline constexpr std::uint8_t kBossAudienceType=70, kBossAudienceFlags=0, kBossAudienceRevision=1;

inline constexpr std::uint16_t kBossSquad=46, kBossActor=47;
inline constexpr std::uint8_t kBossSquadType=1, kBossActorType=2;
inline constexpr std::uint32_t kBossActorGeneration=1;
inline constexpr std::uint16_t kStoryBossSquad=48, kStoryBossActor=49;

inline constexpr std::uint16_t kMinotaurSquad=167;

inline constexpr std::uint16_t kSceneMinotaur=168;
inline constexpr std::uint8_t kSceneType=43;
inline constexpr std::uint32_t kSceneMinotaurConfigTag=0x80F54D68U, kSceneMinotaurResourceTag=0x80F582FEU;
inline constexpr std::uint16_t kSceneMinotaurStory=169;
inline constexpr std::uint32_t kSceneMinotaurStoryConfigTag=0x80F54D6BU;

// hop-on attachment absorbs the parent's damage while attached, so only the crossing makes
// the boss untouchable; the other invincibility component is never used
inline constexpr std::uint16_t kHopOnInvincible=166;
inline constexpr std::uint8_t kHopOnInvincibleType=26;

inline constexpr float kBossGateRoom1=2.F/3.F, kBossGateRoom2=1.F/3.F;

inline constexpr std::array<std::uint8_t,8> kRoom1Groups{4,5,6,7,8,9,10,15};
inline constexpr std::array<std::uint8_t,6> kRoom2Groups{2,3,11,12,13,16};
inline constexpr std::array<std::uint8_t,3> kRoom3Groups{0,1,14};

inline constexpr std::uint8_t kGateType=23;
inline constexpr float kGateBlocking=1.F, kGatePassable=0.F;

// firstAdd/lastAdd are 1-based roster indices; the encounter masks key on 1<<(index-1)
struct BossRoom final {
    std::uint8_t index;
    std::span<const std::uint8_t> groups;
    std::uint8_t arrivalGroup;
    std::uint16_t entry,exit,monitor;
    std::uint8_t firstAdd,lastAdd,shield;
    std::uint8_t retreatCue,arrivalCue;
    std::uint32_t directive;
    coo::Asset directiveTarget;
    bool directiveOnRetreat;
    std::uint32_t spawnSet;
    std::uint8_t music;
};
inline constexpr coo::Asset kBossActorTarget{};
inline constexpr std::uint8_t kBossPointType=47;
inline constexpr coo::Asset kApRoom1{kBossPoints,kBossPointsTag,kBossPointType,7};
inline constexpr coo::Asset kApRoom2{kBossPoints,kBossPointsTag,kBossPointType,1};
inline constexpr coo::Asset kApRoom3{kBossPoints,kBossPointsTag,kBossPointType,2};

inline constexpr std::array<BossRoom,3> kBossRooms{{
    {1,kRoom1Groups,15,120,121,kNoBossMonitor,3,17,17,kNoBossCue,22,
        kDefeatThuun,kBossActorTarget,false,0x23BAC4D9U,kBossMusicIntro},
    {2,kRoom2Groups,3,122,123,178,18,32,32,24,25,
        kEvadeVex,kApRoom3,false,0x61D967C9U,kBossMusicRoom2},
    {3,kRoom3Groups,1,124,125,179,33,39,0,26,kNoBossCue,
        kDefeatThuun,kBossActorTarget,true,0x61D967C8U,kBossMusicRoom3},
}};
[[nodiscard]] constexpr const BossRoom* boss_room(std::uint8_t index) noexcept {
    for(const auto& r:kBossRooms) { if(r.index==index) { return &r; } } return nullptr;
}
static_assert([] { for(const auto& r:kBossRooms) {
    bool arrival=false;
    for(std::size_t i=0;i<r.groups.size();++i) {
        if(r.groups[i]>=kBossObjectiveGroups) { return false; }
        for(std::size_t j=0;j<i;++j) { if(r.groups[j]==r.groups[i]) { return false; } }
        if(r.groups[i]==r.arrivalGroup) { arrival=true; }
    }
    if(!arrival) { return false; } }
    return true; }());
static_assert([] { for(const auto& a:kBossRooms) { for(const auto& b:kBossRooms) {
    if(a.index==b.index) { continue; }
    for(const auto g:a.groups) { for(const auto h:b.groups) { if(g==h) { return false; } } } } }
    return true; }());

inline constexpr std::uint8_t kCohortPrefight=1, kCohortParticipants=2, kCohortRoom1=3,
    kCohortRoom2=4, kCohortRoom3=5;
inline constexpr std::uint8_t kBossLastCohort=5;
inline constexpr std::array<Spawn,45> kBossSpawns{{
    {kBossSquad,kBoss,0,0,0,1,kCohortParticipants,false,-1,0,"sq_boss"},
    {kMinotaurSquad,kBoss,1,0,1,1,kCohortParticipants,false,-1,0,"sq_minotaur_intro"},
    {50,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_adds[0]"},
    {51,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_adds[1]"},
    {52,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_adds[2]"},
    {53,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_adds[3]"},
    {54,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_adds[4]"},
    {55,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_adds[5]"},
    {56,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_adds[6]"},
    {73,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_vex[0]"},
    {74,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_vex[1]"},
    {75,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_vex[2]"},
    {76,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_vex[3]"},
    {77,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_vex[4]"},
    {78,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_vex[5]"},
    {79,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_vex[6]"},
    {57,kBoss,1,0,1,1,kCohortRoom1,true,-1,0,"sq_room1_shield_vex"},
    {58,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_adds[0]"},
    {59,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_adds[1]"},
    {60,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_adds[2]"},
    {61,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_adds[3]"},
    {62,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_adds[4]"},
    {63,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_adds[5]"},
    {64,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_adds[6]"},
    {80,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_vex[0]"},
    {81,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_vex[1]"},
    {82,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_vex[2]"},
    {83,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_vex[3]"},
    {84,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_vex[4]"},
    {85,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_vex[5]"},
    {86,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_vex[6]"},
    {65,kBoss,1,0,1,1,kCohortRoom2,true,-1,0,"sq_room2_shield_vex"},
    {66,kBoss,1,0,1,1,kCohortRoom3,true,-1,0,"sq_room3_adds[0]"},
    {67,kBoss,1,0,1,1,kCohortRoom3,true,-1,0,"sq_room3_adds[1]"},
    {68,kBoss,1,0,1,1,kCohortRoom3,true,-1,0,"sq_room3_adds[2]"},
    {69,kBoss,1,0,1,1,kCohortRoom3,true,-1,0,"sq_room3_adds[3]"},
    {70,kBoss,1,0,1,1,kCohortRoom3,true,-1,0,"sq_room3_adds[4]"},
    {71,kBoss,1,0,1,1,kCohortRoom3,true,-1,0,"sq_room3_adds[5]"},
    {72,kBoss,1,0,1,1,kCohortRoom3,true,-1,0,"sq_room3_adds[6]"},
    {40,kBoss,1,0,1,1,kCohortPrefight,true,-1,0,"sq_prefight_skirmish[0]"},
    {41,kBoss,1,0,1,1,kCohortPrefight,true,-1,0,"sq_prefight_skirmish[1]"},
    {42,kBoss,1,0,1,1,kCohortPrefight,true,-1,0,"sq_prefight_skirmish[2]"},
    {43,kBoss,1,0,1,1,kCohortPrefight,true,-1,0,"sq_prefight_skirmish[3]"},
    {44,kBoss,1,0,1,1,kCohortPrefight,true,-1,0,"sq_prefight_skirmish[4]"},
    {45,kBoss,1,0,1,1,kCohortPrefight,true,-1,0,"sq_prefight_skirmish[5]"},
}};
static_assert([] { for(const auto& s:kBossSpawns) {
    if(s.count!=s.loose+s.second || s.second!=0 || s.categories!=1
        || s.cohort==0 || s.cohort>kBossLastCohort || s.tacticalRow!=-1) { return false; } }
    return true; }());
static_assert(kBossSpawns.size()<=63);
static_assert([] { std::size_t zero=0; for(const auto& s:kBossSpawns) { if(s.count==0) { ++zero; } }
    return zero==1 && kBossSpawns[0].source==kBossSquad && kBossSpawns[0].count==0; }());
static_assert([] { for(const auto& r:kBossRooms) {
    if(r.firstAdd<1 || r.lastAdd<r.firstAdd || r.lastAdd>kBossSpawns.size()) { return false; }
    if(r.shield!=0 && (r.shield<r.firstAdd || r.shield>r.lastAdd)) { return false; } }
    return true; }());
inline constexpr std::uint8_t kFirstPrefightRosterIndex=40, kLastPrefightRosterIndex=45;
static_assert(kBossSpawns[kFirstPrefightRosterIndex-1].cohort==kCohortPrefight
    && kBossSpawns[kLastPrefightRosterIndex-1].cohort==kCohortPrefight
    && kLastPrefightRosterIndex==kBossSpawns.size());
[[nodiscard]] constexpr const Spawn* boss_spawn(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& s:kBossSpawns) { if(s.registry==registry && s.source==slot) { return &s; } } return nullptr;
}
[[nodiscard]] constexpr std::span<const Spawn> room_adds(const BossRoom& room) noexcept {
    return std::span<const Spawn>{kBossSpawns}.subspan(room.firstAdd-1U,
        static_cast<std::size_t>(room.lastAdd-room.firstAdd+1U));
}

inline constexpr std::uint8_t kLaserChannelType=24, kBossLaserObjectType=4;
inline constexpr std::array<std::uint16_t,5> kLaserChannelsRm1B1{189,190,191,192,193};
inline constexpr std::array<std::uint16_t,5> kLaserChannelsRm1B2{194,195,196,197,198};
inline constexpr std::array<std::uint16_t,5> kLaserChannelsRm2B1{199,200,201,202,203};
inline constexpr std::array<std::uint16_t,5> kLaserChannelsRm2B2{204,205,206,207,208};
inline constexpr std::array<std::uint16_t,10> kLaserChannelsRm3B1{209,210,211,212,213,214,215,216,217,218};
inline constexpr std::array<std::uint16_t,10> kLaserChannelsRm3B2{219,220,221,222,223,224,225,226,227,228};
inline constexpr std::array<std::uint16_t,4> kLaserObjectsRm1B1{87,88,89,90};
inline constexpr std::array<std::uint16_t,5> kLaserObjectsRm1B2{91,92,93,94,95};
inline constexpr std::array<std::uint16_t,5> kLaserObjectsRm2B1{96,97,98,99,100};
inline constexpr std::array<std::uint16_t,4> kLaserObjectsRm2B2{101,102,103,104};
inline constexpr std::array<std::uint16_t,8> kLaserObjectsRm3B1{105,106,107,108,109,110,111,112};
inline constexpr std::array<std::uint16_t,7> kLaserObjectsRm3B2{113,114,115,116,117,118,119};
struct LaserBank final {
    std::uint8_t room,bank;
    std::span<const std::uint16_t> channels,objects;
};
inline constexpr std::array<LaserBank,6> kBossLaserBanks{{
    {1,1,kLaserChannelsRm1B1,kLaserObjectsRm1B1},
    {1,2,kLaserChannelsRm1B2,kLaserObjectsRm1B2},
    {2,1,kLaserChannelsRm2B1,kLaserObjectsRm2B1},
    {2,2,kLaserChannelsRm2B2,kLaserObjectsRm2B2},
    {3,1,kLaserChannelsRm3B1,kLaserObjectsRm3B1},
    {3,2,kLaserChannelsRm3B2,kLaserObjectsRm3B2},
}};
[[nodiscard]] constexpr const LaserBank* laser_bank(std::uint8_t room,std::uint8_t bank) noexcept {
    for(const auto& b:kBossLaserBanks) { if(b.room==room && b.bank==bank) { return &b; } } return nullptr;
}
inline constexpr std::size_t kLaserChannelSlots=40, kLaserObjectSlots=33, kDeadLaserChannels=7;
static_assert([] { std::size_t c=0,o=0; for(const auto& b:kBossLaserBanks) { c+=b.channels.size();o+=b.objects.size(); }
    return c==kLaserChannelSlots && o==kLaserObjectSlots && c-o==kDeadLaserChannels; }());
// these seven declarations have no target and no native channel; keep them out of authority
[[nodiscard]] constexpr bool live_laser_channel(std::uint16_t slot) noexcept {
    return slot>=189 && slot<=228 && slot!=193 && slot!=208 && slot!=217 && slot!=218
        && slot!=226 && slot!=227 && slot!=228;
}

inline constexpr std::int32_t kLaserOn=100, kLaserOff=0;
// reconstructed from footage, not recovered from the client profile or SDK export;
// one host clock drives every armed room so a new room joins the next rising edge
inline constexpr std::uint32_t kLaserWarmupMs=2500, kLaserRedHoldMs=7000;
inline constexpr std::uint32_t kLaserFadeMs=1500, kLaserDarkHoldMs=2000;
inline constexpr std::uint32_t kLaserHighMs=kLaserWarmupMs+kLaserRedHoldMs;
inline constexpr std::uint32_t kLaserLowMs=kLaserFadeMs+kLaserDarkHoldMs;
static_assert(kLaserHighMs==9500 && kLaserLowMs==3500);

inline constexpr std::array<Volume,2> kBossVolumes{{
    {kBossApproach,4,"pt_reached_boss",{1592.71692F,-1492.88623F,5.5F},{1623.F,-1471.5F,14.8846169F}},
    {kBoss,232,"tv_room2",{1697.74951F,-1541.28516F,-11.9999924F},{1797.03528F,-1448.28015F,28.0000076F}},
}};
[[nodiscard]] constexpr const Volume* boss_volume(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& v:kBossVolumes) { if(v.registry==registry && v.slot==slot) { return &v; } } return nullptr;
}
inline constexpr std::uint16_t kReachedBossTrigger=0;
inline constexpr std::uint8_t kBossTriggerType=31, kBossMonitorType=30;

inline constexpr std::uint16_t kBossChest=172;
inline constexpr std::uint8_t kBossChestType=4;

inline constexpr std::uint32_t kLifetimeTimerTag=0x80FD25E7U;
inline constexpr std::uint16_t kLifetimeTimerSlot=2;
inline constexpr std::uint8_t kLifetimeTimerType=18;

struct EndingPhase final { std::uint8_t state; std::uint32_t milliseconds; std::string_view where; };
inline constexpr std::array<EndingPhase,3> kBossEndingPhases{{
    {6,30000,"boss.lua defeat - loot and dialogue window"},
    {7,30000,"boss.lua on_timer, leaving phase 6 - native results"},
    {8,0,"boss.lua on_timer, leaving phase 7 - orbit request, no further deadline"},
}};
static_assert(kBossEndingPhases.back().milliseconds==0);

inline constexpr std::uint8_t kCueReveal=20, kCueSceneFinished=21, kCueRoom1=22, kCueRetreatRoom2=24,
    kCueRoom2=25, kCueRetreatRoom3=26, kCueBossDead=27;
} // namespace dawn::state::activity::strike_pact
