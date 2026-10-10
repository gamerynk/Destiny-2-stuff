#pragma once
#include "controller.h"

namespace dawn::state::activity::nessus::failsafe_presentation {
inline constexpr std::uint32_t kRegistry=0x7B3D65F8U,kSourceDescriptor=0x80C030D0U,kIdleDescriptor=0x80C030D6U;
inline constexpr std::uint16_t kSource=0,kRule=UINT16_MAX,kProgram=2,kMember=UINT16_MAX;
inline constexpr std::uint32_t kIdle=0x5D354105U,kGreeting=0xF3999068U;
inline constexpr std::uint32_t kSourceDefinitionOffset=0x878U,kProgramDefinitionOffset=0x228U;
inline constexpr std::array<float,3> kPosition{1148.446044922F,-135.627197266F,-16.237630844F};
inline constexpr bool kApproach=false;
[[nodiscard]] inline std::uint32_t generation(const Frame& frame) noexcept {
    const auto index=source_index(kRegistry,kSource);
    return frame.enabled&&index<frame.sources.size()?frame.sources[index].generation:0;
}
// production excludes this vendor from approach evaluation; never synthesize the greeting
[[nodiscard]] inline constexpr bool nearby(const std::array<float,3>&,bool) noexcept{return false;}
[[nodiscard]] inline constexpr bool owns(std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    return key==kRegistry&&((type==1&&slot==kSource)||(type==42&&slot==kProgram));
}
[[nodiscard]] inline std::size_t body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    if(!owns(key,type,slot)||!generation(frame))return 0;
    return type==1?coo::native_combatant::kSourceBits:101U;
}
template<class Writer>[[nodiscard]] bool write_body(Writer& writer,const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    if(!failsafe_presentation::body_bits(frame,key,type,slot))return false;
    if(type==1){const auto index=source_index(kRegistry,kSource);coo::native_combatant::Source source{kRegistry,generation(frame),0,frame.sources[index].first};source.hasRule=false;return coo::native_combatant::write_source(writer,source);}
    const auto start=writer.bit_count();
    return writer.write(1,1)&&writer.write(kIdle,32)&&writer.write(0x811C9DC5U,32)&&writer.write(0x80000001U,32)
        &&writer.write(1,1)&&writer.write(0,3)&&writer.bit_count()-start==101;
}
}

