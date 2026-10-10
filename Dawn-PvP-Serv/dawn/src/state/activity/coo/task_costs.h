#pragma once
#include <array>
#include <cstdint>

namespace dawn::state::activity::coo {
// squad sense sends deltas, so keep omitted costs and drop all of them when the evaluator
// revision changes; 127 is the native unreachable code
struct TaskCosts final {
    std::array<std::uint8_t,24> cost{};
    std::uint32_t mask{},revision{};
    bool hasRevision{},initialized{};
    void merge(const TaskCosts& report) noexcept {
        if(!report.initialized) { *this={};return; }
        initialized=true;
        if(report.hasRevision) {
            if(hasRevision && revision!=report.revision) { cost={};mask=0; }
            revision=report.revision;hasRevision=true;
        }
        for(std::size_t i=0;i<cost.size();++i) {
            if(report.mask&(std::uint32_t{1}<<i)) { cost[i]=report.cost[i];mask|=std::uint32_t{1}<<i; }
        }
    }
    template<class Allowed>
    std::int8_t select(std::int8_t current,std::uint32_t expected,Allowed allowed) const noexcept {
        if(!hasRevision || revision!=expected) { return current; }
        int best=-1;std::uint8_t price=127;
        for(std::uint8_t row=0;row<cost.size();++row) {
            if(!allowed(row) || !(mask&(std::uint32_t{1}<<row)) || cost[row]>=price) { continue; }
            best=row;price=cost[row];
        }
        if(current>=0 && current<static_cast<int>(cost.size()) && allowed(static_cast<std::uint8_t>(current))) {
            if(best<0 || ((mask&(std::uint32_t{1}<<current)) && cost[current]==price)) { return current; }
        }
        return static_cast<std::int8_t>(best);
    }
};
}
