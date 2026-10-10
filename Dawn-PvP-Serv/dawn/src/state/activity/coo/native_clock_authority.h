#pragma once
#include <bit>
#include <cstdint>
#include <limits>
#include "native_activity_clock.h"
namespace dawn::state::activity::coo::native_clock {
inline constexpr std::uint64_t kTicksPerSecond=673200;
constexpr std::uint64_t ticks(std::uint64_t ms) noexcept {
    return native_activity_ticks(ms);
}
// message 5 carries the activity timestamp as eight raw little-endian bytes
template<class Writer> bool stamp(Writer& w,std::uint64_t value) noexcept {
    for(unsigned shift=0;shift<64;shift+=8) { if(!w.write((value>>shift)&255U,8)) { return false; } }
    return true;
}
template<class Writer> bool countdown(Writer& w,bool running,std::uint64_t epoch,std::uint32_t durationMs=30000) noexcept {
    const auto duration=ticks(durationMs);
    return w.write(running?1U:0U,1) && w.write(0,64) && w.write(duration,64)
        && w.write(0,64) && w.write(duration,64) && w.write(epoch,64)
        && w.write(std::bit_cast<std::uint32_t>(1.F),32) && w.write(0,1)
        && w.write(0x80000000U,32);
}
}
