#pragma once
#include "controller.h"
namespace dawn::state::activity::io::asher_presentation {
inline constexpr std::uint32_t kRegistry=0x36E4495DU,kSourceDescriptor=0x80BD6EDCU,kMemberDescriptor=0x80BD7599U,kIdleDescriptor=0x80BD33C9U;
inline constexpr std::uint16_t kSource=0,kRule=4,kMember=1,kProgram=3;
inline constexpr std::uint32_t kIdle=0xF46E1578U,kGreeting=0xF3999068U;
inline constexpr std::uint32_t kSourceDefinitionOffset=0x728U,kMemberDefinitionOffset=0xB58U,kProgramDefinitionOffset=0x228U;
inline constexpr std::array<float,3> kPosition{1378.385620117F,404.698059082F,3.850521088F};
inline constexpr bool kApproach=true;
[[nodiscard]] inline std::uint32_t generation(const Frame& frame) noexcept {const auto i=source_index(kRegistry,kSource);return frame.enabled&&i<frame.sources.size()?frame.sources[i].generation:0;}
[[nodiscard]] inline bool nearby(const std::array<float,3>& position,bool previous) noexcept {
    const double x=double(position[0])-kPosition[0],y=double(position[1])-kPosition[1],z=double(position[2])-kPosition[2];
    const double radius=previous?8.:6.;return x*x+y*y<=radius*radius&&z*z<=9.;
}
[[nodiscard]] inline constexpr bool owns(std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {return key==kRegistry&&((type==1&&slot==kSource)||(type==2&&slot==kMember)||(type==42&&slot==kProgram));}
[[nodiscard]] inline std::size_t body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot,bool approached=false) noexcept {
    if(!owns(key,type,slot)||!generation(frame))return 0;
    return type==1?coo::native_combatant::kSourceBits:type==2?coo::native_combatant::kBindBits:approached?165U:101U;
}
template<class Writer>[[nodiscard]] bool write_body(Writer& writer,const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot,bool approached=false) noexcept {
    if(!asher_presentation::body_bits(frame,key,type,slot,approached))return false;
    if(type==1){coo::native_combatant::Source source{kRegistry,generation(frame),kRule,0};source.hasRule=true;source.memberOwned=true;return coo::native_combatant::write_source(writer,source);}
    if(type==2)return coo::native_combatant::write_spawn(writer,generation(frame));
    const auto start=writer.bit_count();if(!writer.write(1,1)||!writer.write(kIdle,32)||!writer.write(0x811C9DC5U,32)||!writer.write(0x80000001U,32)||!writer.write(1,1)||!writer.write(approached?1U:0U,3))return false;
    if(approached&&(!writer.write(kIdle,32)||!writer.write(kGreeting,32)))return false;
    return writer.bit_count()-start==asher_presentation::body_bits(frame,key,type,slot,approached);
}
[[nodiscard]] inline std::size_t dispatch_body_bits(const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot,bool approached=false) noexcept {return owns(key,type,slot)?asher_presentation::body_bits(frame,key,type,slot,approached):io::body_bits(frame,key,type,slot);}
template<class Writer>[[nodiscard]] bool dispatch_write_body(Writer& writer,const Frame& frame,std::uint32_t key,std::uint8_t type,std::uint16_t slot,bool approached=false) noexcept {return owns(key,type,slot)?asher_presentation::write_body(writer,frame,key,type,slot,approached):io::write_body(writer,frame,key,type,slot);}
}
