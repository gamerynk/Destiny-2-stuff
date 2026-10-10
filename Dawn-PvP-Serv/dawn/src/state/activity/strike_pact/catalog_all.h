#pragma once
#include "catalog.h"
#include "catalog_forest.h"
#include "catalog_chase.h"
#include "catalog_ledge.h"
#include "catalog_boss.h"
#include "../nightfall/enemy_variants.h"
namespace dawn::state::activity::strike_pact {

// the prefight skirmish sources exist in both section catalogs; one flat ledger needs one
// row per source, so the ledge copy is dropped here
inline constexpr std::uint8_t kLedgePrefightCohort=kCohortPrefightSkirmish;

inline constexpr std::uint8_t kOpeningCohortBase=0;
inline constexpr std::uint8_t kForestCohortBase=3;
inline constexpr std::uint8_t kChaseCohortBase=4;
inline constexpr std::uint8_t kLedgeCohortBase=7;
inline constexpr std::uint8_t kBossCohortBase=11;
inline constexpr std::uint8_t kAllLastCohort=kBossCohortBase+kBossLastCohort;

inline constexpr std::size_t kAllSpawnCount=
    kSpawns.size()+kForestSpawns.size()+kChaseSpawns.size()+(kLedgeSpawns.size()-6)+kBossSpawns.size();

[[nodiscard]] consteval std::array<Spawn,kAllSpawnCount> build_all_spawns() noexcept {
    std::array<Spawn,kAllSpawnCount> out{};
    std::size_t next=0;
    const auto append=[&out,&next](std::span<const Spawn> rows,std::uint8_t base,int drop) noexcept {
        for(const auto& row:rows) {
            if(drop>=0 && row.cohort==static_cast<std::uint8_t>(drop)) { continue; }
            out[next]=row;
            out[next].cohort=static_cast<std::uint8_t>(row.cohort+base);
            ++next;
        }
    };
    append(kSpawns,kOpeningCohortBase,-1);
    append(kForestSpawns,kForestCohortBase,-1);
    append(kChaseSpawns,kChaseCohortBase,-1);
    append(kLedgeSpawns,kLedgeCohortBase,static_cast<int>(kLedgePrefightCohort));
    append(kBossSpawns,kBossCohortBase,-1);
    return out;
}
inline constexpr auto kAllSpawns=build_all_spawns();

inline constexpr std::array<nightfall::EnemySubstitution,18> kGrandmasterEnemySubstitutions{{
    {0xA5F083B5U,4,0x6FC008B5U,0x80C1ACAFU,0x8161FED1U},
    {0xA5F083B5U,19,0xDFD659ACU,0x80C1A8E4U,0x8161FED1U},
    {0xA5F083B5U,38,0xA21827B6U,0x80C0D298U,0x8157862EU},
    {0xA5F083B5U,42,0x7817E226U,0x80C1A8E4U,0x8161FED1U},
    {0xA5F083B5U,44,0x6116E417U,0x80C0D09FU,0x81578798U},
    {0xA5F083B5U,56,0x7817E226U,0x80C1A8E4U,0x8161FED1U},
    {0xA5F083B5U,57,0xA21827B6U,0x80C0D298U,0x8157862EU},
    {0xA5F083B5U,65,0xA21827B6U,0x80C0D298U,0x8157862EU},
    {0xA5F083B5U,73,0x6116E417U,0x80C0D09FU,0x81578798U},
    {0xA5F083B5U,74,0x6116E417U,0x80C0D09FU,0x81578798U},
    {0xA5F083B5U,80,0x6116E417U,0x80C0D09FU,0x81578798U},
    {0xA5F083B5U,81,0x6116E417U,0x80C0D09FU,0x81578798U},
    {0x588E5FB9U,2,0xD3D162D4U,0x80C19B1FU,0x8161FED1U},
    {0x588E5FB9U,2,0x0EA2CE68U,0x80C19B1FU,0x8161FED1U},
    {0x588E5FB9U,20,0xB4009D19U,0x80C0D0BFU,0x81578798U},
    {0x588E5FB9U,25,0xBF95E58CU,0x80C0FA98U,0x8161FED1U},
    {0x588E5FB9U,27,0xBF95E58CU,0x80C0FA98U,0x8161FED1U},
    {0xCC7A090DU,14,0xBF95E58CU,0x80C0FA98U,0x8161FED1U},
}};
static_assert([]{for(const auto& value:kGrandmasterEnemySubstitutions)
    if(!nightfall::valid(value))return false;
    else {bool found{};for(const auto& source:kAllSpawns)
        found=found || (source.registry==value.registry && source.source==value.source);
        if(!found)return false;}
    return true;}());
[[nodiscard]] constexpr bool grandmaster_substitution_source(
    std::uint32_t registry,std::uint16_t source) noexcept {
    return nightfall::substitution_source(kGrandmasterEnemySubstitutions,registry,source);
}
[[nodiscard]] constexpr const nightfall::EnemySubstitution* grandmaster_substitution(
    std::uint32_t registry,std::uint16_t source,std::uint32_t category,std::uint32_t entity) noexcept {
    return nightfall::substitution(kGrandmasterEnemySubstitutions,registry,source,category,entity);
}

// a repeated (registry, source) pair would make the ledger wait forever on the other row
static_assert([]{
    for(std::size_t i=0;i<kAllSpawns.size();++i) {
        for(std::size_t j=0;j<i;++j) {
            if(kAllSpawns[i].registry==kAllSpawns[j].registry
                && kAllSpawns[i].source==kAllSpawns[j].source) { return false; }
        }
        if(kAllSpawns[i].cohort==0 || kAllSpawns[i].cohort>kAllLastCohort) { return false; }
        if(kAllSpawns[i].count!=kAllSpawns[i].loose+kAllSpawns[i].second) { return false; }
    }
    return true;
}(),"the combined strike source table repeats a source, or carries an out-of-range cohort");

[[nodiscard]] constexpr const Spawn* all_spawn(std::uint32_t registry,std::uint16_t source) noexcept {
    for(const auto& row:kAllSpawns) {
        if(row.registry==registry && row.source==source) { return &row; }
    }
    return nullptr;
}
[[nodiscard]] constexpr std::uint8_t opening_cohort(std::uint8_t local) noexcept { return local+kOpeningCohortBase; }
[[nodiscard]] constexpr std::uint8_t forest_cohort(std::uint8_t local) noexcept { return local+kForestCohortBase; }
[[nodiscard]] constexpr std::uint8_t chase_cohort(std::uint8_t local) noexcept { return local+kChaseCohortBase; }
[[nodiscard]] constexpr std::uint8_t ledge_cohort(std::uint8_t local) noexcept { return local+kLedgeCohortBase; }
[[nodiscard]] constexpr std::uint8_t boss_cohort(std::uint8_t local) noexcept { return local+kBossCohortBase; }

inline constexpr std::size_t kAllVolumeCount=
    std::size(kVolumes)+kForestVolumes.size()+kChaseVolumes.size()+kLedgeVolumes.size()+kBossVolumes.size();

[[nodiscard]] consteval std::array<Volume,kAllVolumeCount> build_all_volumes() noexcept {
    std::array<Volume,kAllVolumeCount> out{};
    std::size_t next=0;
    const auto append=[&out,&next](std::span<const Volume> rows) noexcept {
        for(const auto& row:rows) { out[next++]=row; }
    };
    append(kVolumes); append(kForestVolumes); append(kChaseVolumes);
    append(kLedgeVolumes); append(kBossVolumes);
    return out;
}
inline constexpr auto kAllVolumes=build_all_volumes();
[[nodiscard]] constexpr int volume_region(std::size_t index) noexcept {
    if(index<std::size(kVolumes)) { return kRegion; }
    index-=std::size(kVolumes);
    if(index<kForestVolumes.size()) { return kForestRegion; }
    index-=kForestVolumes.size();
    if(index<kChaseVolumes.size()) { return kChaseRegion; }
    index-=kChaseVolumes.size();
    if(index<kLedgeVolumes.size()+kBossVolumes.size()) { return kLedgeRegion; }
    return -1;
}
static_assert([]{
    for(std::size_t i=0;i<kAllVolumes.size();++i) {
        for(std::size_t j=0;j<i;++j) {
            if(kAllVolumes[i].registry==kAllVolumes[j].registry
                && kAllVolumes[i].slot==kAllVolumes[j].slot) { return false; }
        }
    }
    return true;
}(),"the combined strike volume table names the same trigger twice");

} // namespace dawn::state::activity::strike_pact
