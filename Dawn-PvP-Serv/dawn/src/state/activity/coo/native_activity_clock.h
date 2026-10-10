#pragma once
#include <cstdint>
#include <limits>

namespace dawn::state::activity::coo {
// native 35F080 converts seconds at 673200 ticks/second; the 64-bit field after the
// bubble grants in the authority packet is the same clock the actor timers use
inline constexpr std::uint64_t native_activity_ticks(std::uint64_t milliseconds) noexcept {
    constexpr auto maximum=std::numeric_limits<std::uint64_t>::max();
    const auto whole=milliseconds/10,part=(milliseconds%10)*6732/10;
    if(whole>(maximum-part)/6732) { return maximum; }
    return whole*6732+part;
}
class NativeActivityClock final {
public:
    void reset() noexcept { *this={}; }
    std::uint64_t sample(std::uint64_t now) noexcept {
        if(!started_) { started_=true;origin_=latest_=now; }
        if(now>latest_) { latest_=now; }
        return native_activity_ticks(latest_-origin_);
    }
private:
    std::uint64_t origin_{},latest_{};
    bool started_{};
};
}
