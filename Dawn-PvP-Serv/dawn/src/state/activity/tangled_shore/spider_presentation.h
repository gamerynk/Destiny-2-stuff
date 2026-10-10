#pragma once
#include "controller.h"
namespace dawn::state::activity::tangled_shore::spider_presentation {
inline constexpr std::uint32_t kRegistry=0x6D47E9B3U,kIdle=0x681C2A3AU;
inline constexpr std::uint16_t kSource=1,kRule=9,kIdleSlot=8;
inline constexpr std::array<float,3> kPosition{105.533561707F,-297.446044922F,-48.572269440F};
inline std::uint32_t generation(const Frame& frame) noexcept {
    const auto i=source_index(kRegistry,kSource);return frame.enabled&&i<frame.sources.size()?frame.sources[i].generation:0;
}
inline std::size_t body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    return key==kRegistry&&type==42&&slot==kIdleSlot&&generation(frame)?101:0;
}
template<class Writer>bool write_body(Writer& writer,const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    if(!spider_presentation::body_bits(frame,key,type,slot))return false;
    const auto start=writer.bit_count();
    return writer.write(1,1)&&writer.write(kIdle,32)&&writer.write(0x811C9DC5U,32)&&writer.write(0x80000001U,32)
        &&writer.write(1,1)&&writer.write(0,3)&&writer.bit_count()-start==101;
}
}
