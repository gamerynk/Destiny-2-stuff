#pragma once
#include "executor.h"
namespace dawn::state::activity::coo {
struct MarkerTarget final {
    Asset asset{};std::array<std::uint32_t,4> locator{};
    bool valid() const noexcept { return asset.registry!=0 && asset.type<127 && asset.slot<32768; }
    friend bool operator==(const MarkerTarget&,const MarkerTarget&)=default;
};
struct ObjectiveMarker final { std::uint32_t event{};MarkerTarget target{}; };
struct ObjectiveState final {
    std::uint32_t event{},retiredEvent{},revision{};MarkerTarget marker{};bool active{},published{};
};
class ObjectiveService final {
public:
    void set(std::uint32_t event,MarkerTarget marker={}) noexcept {
        if(!event || event==UINT32_MAX) { return; }
        if(state_.active && state_.event==event && state_.marker==marker) { return; }
        state_.retiredEvent=state_.active?state_.event:state_.retiredEvent;
        state_.event=event;state_.marker=marker;state_.active=state_.published=true;++state_.revision;
    }
    void marker(MarkerTarget target) noexcept { if(state_.marker!=target) { state_.marker=target;++state_.revision; } }
    void clear_marker() noexcept { marker({}); }
    void clear() noexcept {
        if(state_.active || state_.marker.valid()) { state_.retiredEvent=state_.event;state_.active=false;clear_marker();++state_.revision; }
    }
    ObjectiveState state() const noexcept { return state_; }
private:
    ObjectiveState state_{};
};
}
