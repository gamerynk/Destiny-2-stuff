// publishing the objective with row -1 asks the native evaluator to cost every group; the mission
// then republishes the cheapest allowed one, as the accepted host build did
#pragma once
#include "catalog_all.h"
#include "../coo/task_costs.h"
namespace dawn::state::activity::strike_pact {

// costs arrive as deltas and often omit the revision, so merge each report into retained state
using TaskCosts = coo::TaskCosts;
// 127 is the evaluator's maximum code: group unreachable
inline constexpr std::uint8_t kTaskUnreachable=127;

struct TaskObjective final {
    std::uint32_t registry;
    std::uint16_t slot;
    std::uint8_t groups;
    std::uint32_t revision;
};
inline constexpr std::uint32_t kTaskRevision=1;

inline constexpr TaskObjective kOpeningObjective{kOpening,kObjectiveSlot,9,kTaskRevision};
inline constexpr TaskObjective kForestObjective{kForest,kForestObjectiveSlot,kForestObjectiveGroups,kTaskRevision};
inline constexpr TaskObjective kChaseObjective{kChase,kChaseObjectiveSlot,kChaseTaskGroups,kTaskRevision};
inline constexpr TaskObjective kLedgeObjective{kLedge,kLedgeObjectiveSlot,kLedgeTaskGroups,kTaskRevision};
inline constexpr TaskObjective kBossObjective{kBoss,kBossObjectiveSlot,kBossObjectiveGroups,kTaskRevision};

[[nodiscard]] constexpr const TaskObjective* task_objective(const Spawn& source) noexcept {
    if(source.registry==kOpening) { return &kOpeningObjective; }
    if(source.registry==kForest) { return &kForestObjective; }
    if(source.registry==kChase) { return &kChaseObjective; }
    if(source.registry==kLedge) {
        return source.cohort>=kBossCohortBase+1?&kBossObjective:&kLedgeObjective;
    }
    return nullptr;
}

// each boss room's task groups resolve to firing areas in that room only
[[nodiscard]] constexpr bool task_allowed(const Spawn& source,std::uint8_t group) noexcept {
    const auto* objective=task_objective(source);
    if(!objective || group>=objective->groups) { return false; }
    if(objective!=&kBossObjective) { return true; }
    const auto room=source.cohort==boss_cohort(kCohortRoom2)?2
        :source.cohort==boss_cohort(kCohortRoom3)?3:1;
    const auto* rows=boss_room(static_cast<std::uint8_t>(room));
    if(!rows) { return false; }
    for(const auto allowed:rows->groups) { if(allowed==group) { return true; } }
    return false;
}

// a delivery creates actors but does not turn an evaluate-only row into a combat task
[[nodiscard]] constexpr bool task_fixed(const Spawn& source) noexcept {
    return source.tacticalRow>=0
        || (source.registry==kBoss && source.source==kBossSquad);
}

[[nodiscard]] constexpr std::int8_t task_initial(const Spawn& source) noexcept {
    if(source.tacticalRow>=0) { return source.tacticalRow; }
    if(source.registry==kBoss && source.source==kBossSquad) {
        return static_cast<std::int8_t>(kBossRooms[0].arrivalGroup);
    }
    return -1;
}

static_assert([]{
    for(const auto& row:kAllSpawns) {
        const auto* objective=task_objective(row);
        if(!objective || objective->groups==0 || objective->groups>24) { return false; }
        const auto initial=task_initial(row);
        if(initial>=0 && !task_allowed(row,static_cast<std::uint8_t>(initial))) { return false; }
    }
    return true;
}(),"a strike source has no combat objective, or starts on a task group it may not take");

[[nodiscard]] constexpr std::size_t all_spawn_index(std::uint32_t registry,std::uint16_t source) noexcept {
    for(std::size_t i=0;i<kAllSpawns.size();++i) {
        if(kAllSpawns[i].registry==registry && kAllSpawns[i].source==source) { return i; }
    }
    return kAllSpawns.size();
}

} // namespace dawn::state::activity::strike_pact
