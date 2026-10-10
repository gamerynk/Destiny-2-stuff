#pragma once
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::native::generic_device {
// exact-build 80805063: producer DF4020, consumer DF6510, reflection 3909B08; nine 32-bit
// fields, signed ones biased 0x80000000. The generic component record, not type23
inline constexpr std::uint32_t kSchema=0x80805063U;
inline constexpr std::size_t kRecordBits=321;
struct Channel final {
    std::int32_t revision{-1},snapRevision{-1};float value{};
    friend constexpr bool operator==(const Channel&,const Channel&)=default;
};
struct State final {
    Channel power{-1,-1,1.F},lock{},position{};
    friend constexpr bool operator==(const State&,const State&)=default;
};
[[nodiscard]] inline bool valid(const State& state) noexcept {
    const auto validChannel=[](const Channel& c) noexcept {
        return c.revision>=-1 && c.snapRevision>=-1 && std::isfinite(c.value);
    };
    return validChannel(state.power) && validChannel(state.lock) && validChannel(state.position);
}
template<class Writer> bool write_record(Writer& writer,const State& state) noexcept {
    if(!valid(state) || !writer.write(1,1) || !writer.write(kSchema,32))return false;
    for(const auto& c:{state.power,state.lock,state.position}) {
        if(!writer.write(std::bit_cast<std::uint32_t>(c.revision)^0x80000000U,32)
            || !writer.write(std::bit_cast<std::uint32_t>(c.snapRevision)^0x80000000U,32)
            || !writer.write(std::bit_cast<std::uint32_t>(c.value),32))return false;
    }
    return true;
}
}
