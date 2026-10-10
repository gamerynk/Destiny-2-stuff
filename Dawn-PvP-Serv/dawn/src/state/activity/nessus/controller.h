#pragma once
#include "catalog.h"
#include "../coo/native_combatant_authority.h"
#include "../coo/task_costs.h"
#include <array>

namespace dawn::state::activity::nessus {
struct SourceState final {std::uint32_t generation{};std::uint8_t first{},second{};std::int8_t tacticalRow{-1};};
struct Frame final {
    std::array<SourceState,kPopulations.size()> sources{};
    std::uint64_t session{},incarnation{},visited{};
    std::uint32_t region{};bool enabled{};
};
[[nodiscard]] constexpr bool valid_region(std::uint32_t region) noexcept {
    if(region>=512 || region%8)return false;
    for(const auto& registry:kRegistries)if(registry.bubble==region/8)return true;
    return false;
}
class Controller final {
public:
    [[nodiscard]] Frame update(std::uint64_t session,std::uint64_t incarnation,
        std::uint32_t heldRegion,bool arrived,std::uint32_t prewarmRegion=UINT32_MAX) noexcept {
        if(!session || !incarnation)return {};
        if(frame_.session!=session || frame_.incarnation!=incarnation) {
            frame_={};costs_={};frame_.session=session;frame_.incarnation=incarnation;
        }
        if(arrived && valid_region(heldRegion)) {
            frame_.region=heldRegion;frame_.enabled=true;start(heldRegion/8,false);
        }
        if(valid_region(prewarmRegion))start(prewarmRegion/8,true);
        return frame_;
    }
    [[nodiscard]] Frame snapshot() const noexcept{return frame_;}
    [[nodiscard]] bool observe_tasks(std::uint64_t session,std::uint64_t incarnation,
        std::uint32_t registry,std::uint16_t source,std::uint32_t revision,const coo::TaskCosts& report) noexcept {
        if(!frame_.enabled || session!=frame_.session || incarnation!=frame_.incarnation)return false;
        for(std::size_t i=0;i<kPopulations.size();++i) {
            const auto& binding=kPopulations[i];auto& state=frame_.sources[i];
            if(kRegistries[binding.registry].key!=registry || binding.source!=source)continue;
            if(!state.generation || revision!=state.generation || !binding.tacticalRows)return false;
            if(report.hasRevision && report.revision!=revision){costs_[i]={};return false;}
            costs_[i].merge(report);
            const auto selected=costs_[i].select(state.tacticalRow,revision,
                [&binding](std::uint8_t row){return row<binding.tacticalRows;});
            if(selected>=0)state.tacticalRow=selected;
            return true;
        }
        return false;
    }
private:
    void start(std::uint32_t bubble,bool patrolOnly) noexcept {
        frame_.visited|=std::uint64_t{1}<<bubble;
        for(std::size_t i=0;i<kPopulations.size();++i) {
            const auto& binding=kPopulations[i];auto& source=frame_.sources[i];
            if(kRegistries[binding.registry].bubble!=bubble || source.generation
                || (patrolOnly && binding.kind==PopulationKind::npc))continue;
            source={1,static_cast<std::uint8_t>(binding.requestOverride?binding.requestOverride:1),binding.secondRequestOverride,binding.tacticalRow};
        }
    }
    Frame frame_{};
    std::array<coo::TaskCosts,kPopulations.size()> costs_{};
};
[[nodiscard]] inline std::size_t source_index(std::uint32_t key,std::uint16_t slot) noexcept {
    for(std::size_t i=0;i<kPopulations.size();++i)
        if(kRegistries[kPopulations[i].registry].key==key && kPopulations[i].source==slot)return i;
    return kPopulations.size();
}
[[nodiscard]] inline std::size_t body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    if(!frame.enabled || type!=1)return 0;
    const auto i=source_index(key,slot);
    if(i==kPopulations.size() || !frame.sources[i].generation)return 0;
    return kPopulations[i].categories==2?coo::native_combatant::kTwoCategorySourceBits:coo::native_combatant::kSourceBits;
}
template<class Writer>[[nodiscard]] bool write_body(Writer& writer,const Frame& frame,
    std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    if(!body_bits(frame,key,type,slot))return false;
    const auto i=source_index(key,slot);const auto& binding=kPopulations[i];const auto& state=frame.sources[i];
    coo::native_combatant::Source source{key,state.generation,binding.rule,state.first};
    source.hasRule=binding.hasRule;source.hasSecondCategory=binding.categories==2;source.secondRequested=state.second;
    if(binding.tacticalRow>=0)source.tactical={key,binding.tactical,state.tacticalRow,state.generation};
    return coo::native_combatant::write_source(writer,source);
}
}
