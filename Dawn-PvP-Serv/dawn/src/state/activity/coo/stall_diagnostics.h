#pragma once
#include "executor.h"
namespace dawn::state::activity::coo {
enum class Missing : std::uint8_t { none,admission,health,ai,tactical,death,capacity,preparation,object,controller,device,sceneBinding,sceneEvent,conversation,eventOrigin,timer,dialogue,observation };
constexpr const char* missing_name(Missing m) noexcept {
    constexpr const char* names[]{"none","enemy_admission","health_binding","ai_binding","tactical_assignment","enemy_death","population_capacity","object_preparation","object_creation","controller_binding","device_state","scene_binding","scene_event","conversation","event_origin","timer","dialogue_dispatch","observation"};
    const auto i=static_cast<unsigned>(m);return i<std::size(names)?names[i]:"unknown";
}
struct StallDetail final {
    Missing missing{};Asset asset{};std::uint32_t expected{},actual{},detail{};
    friend bool operator==(const StallDetail&,const StallDetail&)=default;
};
struct StallReport final { Token token{};StallDetail detail{};std::uint64_t waitingMs{}; };
class StallDiagnostics final {
public:
    void reset() noexcept { *this={}; }
    bool observe(Token token,StallDetail detail,std::uint64_t now,StallReport& report,
                 std::uint64_t threshold=10000,std::uint64_t interval=15000) noexcept {
        if(!token.run || !token.incarnation || token.step>=32 || token.command>=8) { return false; }
        auto& entry=entries_[token.step*8U+token.command];
        if(entry.token!=token || entry.detail!=detail || now<entry.since) { entry={token,detail,now,0,false}; }
        if(detail.missing==Missing::none || now-entry.since<threshold || (entry.emitted && (now<entry.last || now-entry.last<interval))) { return false; }
        entry.last=now;entry.emitted=true;report={token,detail,now-entry.since};return true;
    }
private:
    struct Entry { Token token{};StallDetail detail{};std::uint64_t since{},last{};bool emitted{}; };
    std::array<Entry,256> entries_{};
};
}
