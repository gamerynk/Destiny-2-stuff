#pragma once
#include "executor.h"
#include <span>

namespace dawn::state::activity::coo::native_scene {
inline constexpr std::size_t cast_bits(std::size_t count,std::size_t events=0) noexcept { return 74U+55U*count+32U*events; }
template<class Writer>
bool cast_scene(Writer& writer,std::uint32_t generation,std::span<const Asset> cast,std::span<const std::uint32_t> events={},std::uint32_t sourceRevision=1,bool stop=false) noexcept {
    if(generation>0x7FFFFFFFU || sourceRevision>0x7FFFFFFFU || cast.size()>15 || events.size()>32 || (!generation && (!cast.empty() || !events.empty()))) { return false; }
    for(const auto& target:cast) {
        if(!target.registry || target.registry==0x811C9DC5U || target.type>72 || target.slot>32767) { return false; }
    }
    for(std::size_t i=0;i<events.size();++i) {
        if(events[i]==0 || events[i]==UINT32_MAX) { return false; }
        for(std::size_t j=0;j<i;++j) { if(events[i]==events[j]) { return false; } }
    }
    const auto begin=writer.bit_count();
    if(!writer.write(generation?0x80000000U+generation:0x7FFFFFFFU,32)
        || !writer.write(stop?1U:0U,1) || !writer.write(static_cast<std::uint32_t>(cast.size()),4)) { return false; }
    for(const auto& target:cast) {
        if(!writer.write(target.registry,32) || !writer.write(target.type+1U,7)
            || !writer.write(target.slot+32768U,16)) { return false; }
    }
    if(!writer.write(generation?sourceRevision:0U,31) || !writer.write(static_cast<std::uint32_t>(events.size()),6)) { return false; }
    for(const auto event:events) { if(!writer.write(event,32)) { return false; } }
    return writer.bit_count()-begin==cast_bits(cast.size(),events.size());
}
}
