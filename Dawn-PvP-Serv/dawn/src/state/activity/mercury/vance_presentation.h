#pragma once
#include "controller.h"

namespace dawn::state::activity::mercury::vance_presentation {
inline constexpr std::uint32_t kRegistry=0x564C6ECEU,kSourceDescriptor=0x80F5B9CCU,kIdleDescriptor=0x80F5BA28U;
inline constexpr std::uint16_t kSource=0,kRule=UINT16_MAX,kProgram=2,kMember=UINT16_MAX;
inline constexpr std::uint32_t kIdle=0x010B0F07U,kGreeting=0xF3999068U;
inline constexpr std::uint32_t kSourceDefinitionOffset=0x878U,kProgramDefinitionOffset=0x228U;
inline constexpr std::array<float,3> kPosition{37.833992004F,249.519378662F,311.340515137F};
[[nodiscard]] inline std::uint32_t generation(const Frame& frame) noexcept {
    const auto index=source_index(kRegistry,kSource);
    return frame.enabled&&index<frame.sources.size()?frame.sources[index].generation:0;
}
[[nodiscard]] inline bool nearby(const std::array<float,3>& position,bool previous) noexcept {
    const double x=double(position[0])-kPosition[0],y=double(position[1])-kPosition[1],z=double(position[2])-kPosition[2];
    const double radius=previous?8.:6.;return x*x+y*y<=radius*radius&&z*z<=9.;
}
[[nodiscard]] inline std::size_t body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,
    std::uint16_t slot,bool approached=false) noexcept {
    if(key!=kRegistry||!generation(frame))return 0;
    if(type==1&&slot==kSource)return coo::native_combatant::kSourceBits;
    if(type==42&&slot==kProgram)return approached?165U:101U;
    return 0;
}
template<class Writer>[[nodiscard]] bool write_body(Writer& writer,const Frame& frame,std::uint32_t key,
    std::uint8_t type,std::uint16_t slot,bool approached=false) noexcept {
    if(!body_bits(frame,key,type,slot,approached))return false;
    if(type==1) {
        const auto index=source_index(kRegistry,kSource);
        coo::native_combatant::Source source{kRegistry,generation(frame),kRule,frame.sources[index].first};
        source.hasRule=false;return coo::native_combatant::write_source(writer,source);
    }
    const auto start=writer.bit_count();
    if(!writer.write(1,1)||!writer.write(kIdle,32)||!writer.write(0x811C9DC5U,32)
        ||!writer.write(0x80000000U|generation(frame),32)||!writer.write(1,1)||!writer.write(approached?1U:0U,3))return false;
    if(approached&&(!writer.write(kIdle,32)||!writer.write(kGreeting,32)))return false;
    return writer.bit_count()-start==body_bits(frame,key,type,slot,approached);
}
}
