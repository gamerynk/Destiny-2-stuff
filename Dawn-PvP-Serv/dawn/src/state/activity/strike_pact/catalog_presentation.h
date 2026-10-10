// Presentation identities for all four held regions, from the 80F54AE7 SDK export.
// catalog.h restates the region-120 slice; the static_asserts below keep the two in sync.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include "../coo/dialogue_service.h"
#include "../coo/objective_service.h"
#include "catalog.h"
namespace dawn::state::activity::strike_pact::presentation {
namespace pact = dawn::state::activity::strike_pact;

inline constexpr coo::Asset kDirectiveAsset{kRoot,kRootTag,68,0};
inline constexpr coo::Asset kDialogueAsset{kRoot,kRootTag,53,2};
inline constexpr coo::Asset kMusicAsset{kRoot,kRootTag,11,1};
inline constexpr std::uint32_t kDirectiveSchema=0x80804F67U,kDialogueSchema=0x80804F77U,kMusicSchema=0x80804F58U;
inline constexpr std::size_t kDirectiveBits=4802,kDialogueBits=19767,kMusicBits=7223;

// zero duration makes a placeholder row unplayable (DialogueService::enqueue rejects it)
// no row is sceneOwned: boss.lua plays rows 20 and 21 itself around the Minotaur scene
inline constexpr std::uint32_t kRowMs=7000;
inline constexpr coo::DialogueRow kDialogueRows[]{
    {0xB35F543CU,22201U,0,false},
    {0x94358F13U,9644U,0,false},
    {0,0,0,false},
    {0,0,0,false},
    {0xC04A5765U,11076U,0,false},
    {0x6A30D732U,13067U,0,false},
    {0xC18E182AU,13467U,0,false},
    {0,0,0,false},
    {0,0,0,false},
    {0,0,0,false},
    {0x81D654A2U,9262U,0,false},
    {0x110D4DBFU,9258U,0,false},
    {0,0,0,false},
    {0x4B4B0C95U,2253U,0,false},
    {0,0,0,false},
    {0,0,0,false},
    {0x5276621BU,5823U,0,false},
    {0x274C8AD6U,16540U,0,false},
    {0x52AE09A9U,3786U,0,false},
    {0xA40140B4U,10497U,0,false},
    {0x84D90F6BU,4121U,0,false},
    {0xCE4E6824U,2368U,0,false},
    {0xE5B7C166U,9319U,0,false},
    {0,0,0,false},
    {0xE10541E0U,2646U,0,false},
    {0x56B56FBCU,6612U,0,false},
    {0xED18D16AU,2423U,0,false},
    {0xD1DD2887U,16010U,0,false},
    {0xB61406F2U,3588U,0,false},
    {0x8C046411U,16599U,0,false},
};
inline constexpr std::size_t kDialogueRowCount=std::size(kDialogueRows);
// mission.DialogueCue maps CUE_n -> n, so a cue ordinal is the array index
[[nodiscard]] constexpr bool authored_row(std::uint8_t row) noexcept {
    return row<kDialogueRowCount && kDialogueRows[row].selector!=0;
}
inline constexpr coo::DialogueDefinition kDialogue{kBank,kDialogueRows,{}};

inline constexpr std::uint32_t kDirectiveBank=0x0002767EU;
static_assert(kDirectiveBank==161406U);
inline constexpr std::uint32_t kApproachGateway=0xDA162298U,kTraverseForest=0x489890F3U,kTrackCabal=0x97DFF425U,
    kDisableBarrier=0xEA50F954U,kEliminateHostiles=0x3D6350FAU,kMountUp=0xE133A090U,kFindLeader=0x574D4C17U,
    kFindMap=0xF611984AU,kEvadeVex=0x59365CA0U,kDefeatThuun=0xD1ECAA7BU,kAccessMap=0xF52F2E37U;
inline constexpr std::uint32_t kObjectives[]{kApproachGateway,kTraverseForest,kTrackCabal,kDisableBarrier,
    kEliminateHostiles,kMountUp,kFindLeader,kFindMap,kEvadeVex,kDefeatThuun,kAccessMap};
// objective order follows the retail recording (route.lua): Forest keeps Traverse until the
// Chase publishes Eliminate Hostiles
struct DirectiveUse final { std::uint32_t event; std::string_view symbol,where; };
inline constexpr DirectiveUse kDirectiveUses[]{
    {kApproachGateway,"APPROACH_THE_INFINITE_FOREST_GATEWAY","strike_pact.lua initialize_presentation, opening before the gate opens"},
    {kTraverseForest,"TRAVERSE_THE_INFINITE_FOREST","strike_pact.lua update_gate and pt_enter_tunnel; route.lua forest steps 2-3"},
    {kTrackCabal,"TRACK_THE_CABAL","strike_pact.lua initialize_forest, Forest arrival (audience only, no target)"},
    {kDisableBarrier,"DISABLE_THE_BARRIER","route.lua forest step 1 - see_portal trigger or the forest_end monitor"},
    {kEliminateHostiles,"ELIMINATE_HOSTILES","route.lua chase step 1 - Chase arrival and enter_chase"},
    {kMountUp,"MOUNT_UP_AND_MOVE_QUICKLY","route.lua chase step 2 - firstroom_cleared"},
    {kFindLeader,"FIND_THE_CABAL_LEADER","route.lua chase step 3 (sparrow_run/sparrow_jump) and bomb step 1 (ledge arrival)"},
    {kFindMap,"FIND_THE_MAP_OF_THE_INFINITE_FOREST","route.lua bomb step 2 (ledge_final); boss.lua on_player_trigger before the prefight clears"},
    {kEvadeVex,"EVADE_VEX_DEFENSES","boss.lua room 2, targeting ap_room3"},
    {kDefeatThuun,"DEFEAT_VALUS_THUUN","boss.lua show_objective at fight start and room 3"},
    {kAccessMap,"ACCESS_THE_MAP",{}},
};
static_assert(std::size(kDirectiveUses)==std::size(kObjectives));

// music.select is monotonic: sections at or below the published one are dropped
inline constexpr std::uint8_t kMusicForest=2,kMusicChaseFight=5,kMusicBombStart=8,kMusicBossIntro=10,
    kMusicBossRoom2=12,kMusicBossRoom3=14,kMusicBossDead=16;
struct MusicSection final { std::uint8_t candidate; std::uint16_t region; std::string_view name,where; };
inline constexpr MusicSection kMusicSections[]{
    {kMusicForest,72,"FOREST","route.lua route.initialize - Forest arrival"},
    {kMusicChaseFight,16,"CHASE_FIGHT","route.lua stage enter_chase"},
    {kMusicBombStart,0,"BOMB_START","route.lua route.initialize - ledge arrival"},
    {kMusicBossIntro,0,"BOSS_INTRO","boss.lua reveal"},
    {kMusicBossRoom2,0,"BOSS_ROOM2_FIGHT","boss.lua start_room 2"},
    {kMusicBossRoom3,0,"BOSS_ROOM3_FIGHT","boss.lua start_room 3"},
    {kMusicBossDead,0,"BOSS_DEAD","boss.lua defeat"},
};
[[nodiscard]] constexpr bool advances_music(std::int16_t published,std::uint8_t section) noexcept {
    return static_cast<std::int32_t>(section)>published;
}
static_assert([] { std::int16_t last=-1; for(const auto& s:kMusicSections) { if(!advances_music(last,s.candidate)) { return false; } last=static_cast<std::int16_t>(s.candidate); } return true; }());
// candidate name hashes are not in the SDK export (only the slot record), so only ordinals are kept

// region_index is 8x the state's key ordinal (the bubble) in every export row; separate fields
// a bubble without its own spawn set has no respawn location, so each region selects one
struct Region final {
    std::string_view state,name;
    std::uint32_t stateHash,registry,tag,arrivalSpawnSet;
    std::uint16_t regionIndex,sliceSetIndex,audience;
    std::uint8_t bubble;
};
inline constexpr Region kRegions[]{
    // the opening never calls set_checkpoint; it keeps the lifetime's starting spawn set
    {"STATE_80F54AE7_000F_0000_80F54AE2","opening",0x02F12469U,kOpening,kOpeningTag,kOpeningSpawnSet,120,120,64,15},
    {"STATE_80F54AE7_0009_0000_80F54ADC","forest",0xA522CBE0U,0x2763EC91U,0x80F550B8U,0xFEB62946U,72,72,28,9},
    // chase selects nothing on arrival; its first checkpoint waits for enter_chase so a fly-in
    // cannot strand the respawn on a set the player has not reached
    {"STATE_80F54AE7_0002_0000_80F54AD5","chase",0x95E28335U,0x588E5FB9U,0x80F54FFEU,0U,16,16,93,2},
    {"STATE_80F54AE7_0000_0000_80F54AD3","ledge_and_boss",0xDA8AE171U,0xA5F083B5U,0x80F54E07U,0x3360C6BEU,0,0,176,0},
};
static_assert([] { for(const auto& r:kRegions) { if(r.regionIndex!=r.sliceSetIndex || r.regionIndex!=8U*r.bubble) { return false; } } return true; }());
[[nodiscard]] constexpr const Region* region(std::uint16_t index) noexcept {
    for(const auto& r:kRegions) { if(r.regionIndex==index) { return &r; } } return nullptr;
}
// spawn sets are mission decisions from route.lua and boss.lua; the SDK export has no spawn-set table
struct Checkpoint final { std::uint16_t region; std::uint32_t spawnSet; std::string_view where; };
inline constexpr Checkpoint kCheckpoints[]{
    {72,0xFEB62946U,"route.lua route.initialize - Forest entrance"},
    {72,0x54A5A748U,"route.lua stage see_portal - Forest exit"},
    {16,0xC803F1CCU,"route.lua stage enter_chase"},
    {16,0x4DA08186U,"route.lua firstroom_cleared"},
    {0,0x3360C6BEU,"route.lua route.initialize - ledge"},
    {0,0x23BAC4D9U,"boss.lua on_player_trigger - pt_reached_boss"},
    {0,0x61D967C9U,"boss.lua start_room 2"},
    {0,0x61D967C8U,"boss.lua start_room 3"},
};

// each cue plays at most once per run, so a re-entered volume or reattach must not re-offer it
struct CuePoint final { std::uint8_t row; std::uint16_t region; std::string_view where; };
inline constexpr CuePoint kCuePoints[]{
    {0,120,"strike_pact.lua start_dialogue"},          {1,120,"strike_pact.lua pt_near_gate"},
    {4,120,"strike_pact.lua pt_enter_tunnel"},         {5,72,"strike_pact.lua pt_begin"},
    {6,72,"strike_pact.lua on_event_generator_state"}, {10,16,"route.lua enter_chase"},
    {11,16,"route.lua firstroom_cleared"},             {13,16,"route.lua sparrow_jump"},
    {17,0,"route.lua route.initialize - ledge"},       {18,0,"route.lua ledge_final"},
    {19,0,"route.lua see_tree"},                       {20,0,"boss.lua reveal"},
    {21,0,"boss.lua on_scene_finished"},               {22,0,"boss.lua start_room1"},
    {24,0,"boss.lua retreat to room 2"},               {25,0,"boss.lua room 2"},
    {26,0,"boss.lua retreat to room 3"},               {27,0,"boss.lua defeat"},
};
static_assert([] { for(const auto& c:kCuePoints) { if(!authored_row(c.row) || !region(c.region)) { return false; } } return true; }());

// the boss actor SQ_BOSS_VAL_THOOUN (type 2, slot 47 of 0xA5F083B5) is followed by boss.lua
// and is deliberately not in this table
inline constexpr std::uint16_t kNavPointType=47;
struct NavPoint final { std::uint32_t registry,tag; std::uint16_t slot; std::string_view name,use; };
inline constexpr NavPoint kNavPoints[]{
    {kLighthouse,kLighthouseTag,8,"ap_forest_portal","strike_pact.lua show_directive - the opening's gateway marker, dropped once through the tunnel"},
    {0xA9350228U,0x80F550C9U,5,"ap_forest_portal","route.lua forest default target"},
    {0x7E558786U,0x80F550DAU,7,"ap_forest_portal","route.lua stage leaving_forest - the far end of the exit tunnel"},
    {0x8E64DB66U,0x80F55029U,4,"ap_fight_room","route.lua chase target while Eliminate Hostiles is published"},
    {0xFBD01A06U,0x80F55018U,6,"ap_boss_room","route.lua chase default target"},
    {0x73CBF939U,0x80F54E12U,1,"ap_boss_room","route.lua bomb default target"},
    {0xC6F46FAFU,0x80F54E1DU,7,"slot_0007","boss.lua on_player_trigger - the room-1 approach"},
    {0xC6F46FAFU,0x80F54E1DU,2,"slot_0002","boss.lua Evade Vex defenses - the room-3 gauntlet"},
};
// unused marker slots need the FNV absent sentinel; zero is a legal locator and reads as a real position
inline constexpr std::array<std::uint32_t,4> kAbsentLocator{0x811C9DC5U,0x811C9DC5U,0x811C9DC5U,0x811C9DC5U};
[[nodiscard]] constexpr coo::MarkerTarget marker(const NavPoint& point) noexcept {
    return {{point.registry,point.tag,kNavPointType,point.slot},kAbsentLocator};
}
[[nodiscard]] constexpr const NavPoint* nav_point(std::uint32_t registry,std::uint16_t slot) noexcept {
    for(const auto& p:kNavPoints) { if(p.registry==registry && p.slot==slot) { return &p; } } return nullptr;
}
static_assert([] { for(const auto& p:kNavPoints) { if(p.registry==0 || kNavPointType>=127 || p.slot>=32768) { return false; } } return true; }());

static_assert(kRowMs==pact::kRowMs && kDialogueRowCount==std::size(pact::kDialogueRows));
static_assert([] { for(std::size_t i=0;i<kDialogueRowCount;++i) { if(kDialogueRows[i].selector!=pact::kDialogueRows[i].selector) { return false; } } return true; }());
static_assert(std::size(kObjectives)==std::size(pact::kObjectives));
static_assert([] { for(std::size_t i=0;i<std::size(kObjectives);++i) { if(kObjectives[i]!=pact::kObjectives[i]) { return false; } } return true; }());
static_assert(kRegions[0].regionIndex==pact::kRegion && kRegions[0].bubble==pact::kBubble
    && kRegions[0].audience==pact::kAudience && kRegions[0].arrivalSpawnSet==pact::kOpeningSpawnSet);
} // namespace dawn::state::activity::strike_pact::presentation
