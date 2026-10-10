#pragma once
#include "activity_clock_authority.h"

namespace dawn::middleware::bap::activity_message::native::capture_controller {
// timed device state 80804FCA, dispatched within the type4 source's 80809AEA list by
// 9EF680/B31960/FEE1C0; 1003900 copies the tuple, 1006F20 updates progress, time and latch
inline constexpr std::uint32_t kSchema=0x80804FCA;
inline constexpr std::size_t kClockBits=353,kPayloadBits=355,kRecordBits=388;
struct SharedClock final {
    bool running{};
    std::uint64_t minimum{},maximum{},elapsed{},remaining{},anchor{};
    float rate{};
    friend constexpr bool operator==(const SharedClock&,const SharedClock&)=default;
};
struct State final {
    bool active{};
    SharedClock clock{};
    bool field2{};
    friend constexpr bool operator==(const State&,const State&)=default;
};
[[nodiscard]] inline bool valid(const State& value) noexcept {
    const auto& c=value.clock;
    return std::isfinite(c.rate) && c.minimum<=c.maximum
        && c.elapsed>=c.minimum && c.elapsed<=c.maximum
        && (!c.running || (c.anchor!=UINT64_MAX && c.rate!=0.0F));
}
template<class Writer> [[nodiscard]] bool write_payload(Writer& writer,const State& value) noexcept {
    if(!valid(value))return false;
    const auto& c=value.clock;
    return writer.write(value.active?1:0,1) && writer.write(c.running?1:0,1)
        && writer.write(c.minimum,64) && writer.write(c.maximum,64)
        && writer.write(c.elapsed,64) && writer.write(c.remaining,64)
        && writer.write(c.anchor,64) && writer.write(std::bit_cast<std::uint32_t>(c.rate),32)
        && writer.write(value.field2?1:0,1);
}
// writes ONE present dynamic-list record; the owning placement encoder must count it in its
// 2-bit list length and keep other authorized overrides
template<class Writer> [[nodiscard]] bool write_record(Writer& writer,const State& value) noexcept {
    return valid(value) && writer.write(1,1) && writer.write(kSchema,32)
        && write_payload(writer,value);
}
}
