#pragma once
#include <cstdint>
#include <span>
namespace dawn::state::activity::coo::native_scene {
template<class Writer> bool scene(Writer& w,std::uint32_t registry,std::uint16_t source,std::uint32_t generation,
    std::span<const std::uint32_t> events={}) noexcept {
    if(generation>0x7FFFFFFFU || events.size()>32 || (!generation && !events.empty())) { return false; }
    for(std::size_t i=0;i<events.size();++i) {
        if(events[i]==0 || events[i]==UINT32_MAX) { return false; }
        for(std::size_t j=0;j<i;++j) { if(events[j]==events[i]) { return false; } }
    }
    const auto begin=w.bit_count();
    if(!w.write(generation?0x80000000U+generation:0x7FFFFFFFU,32) || !w.write(0,1)
        || !w.write(generation?1U:0U,4)) { return false; }
    if(generation && (!w.write(registry,32) || !w.write(2,7) || !w.write(32768U+source,16))) { return false; }
    if(!w.write(generation?1U:0U,31) || !w.write(static_cast<std::uint32_t>(events.size()),6)) { return false; }
    for(const auto event:events) { if(!w.write(event,32)) { return false; } }
    return w.bit_count()-begin==(generation?129U:74U)+32U*events.size();
}
template<class Writer>
[[nodiscard]] bool write_source(Writer& writer,std::uint32_t generation,
                                bool requested) noexcept {
    if(generation==0 || generation>0x7FFFFFFFU) { return false; }
    const auto begin=writer.bit_count();
    const auto absent=[&writer]() noexcept {
        return writer.write(1,1) && writer.write(0x811C9DC5U,32)
            && writer.write(0,7) && writer.write(32767U,16);
    };
    const bool ok=absent() && absent()
        && writer.write(1,1) && writer.write(0,3)
        && writer.write(1,1) && writer.write(1,4)
        && writer.write(0x80000000U+(requested?1U:0U),32)
        && writer.write(1,1) && writer.write(0,4)
        && writer.write(1,1) && writer.write(1,3) && writer.write(1,2)
        && writer.write(1,3) && writer.write(1,2) && writer.write(1,3)
        && writer.write(1,1) && writer.write(generation,31)
        && writer.write(1,1) && writer.write(0,32)
        && writer.write(1,1) && writer.write(0x811C9DC5U,32)
        && absent() && absent() && absent() && absent()
        && writer.write(1,1) && writer.write(0,31)
        && writer.write(1,1) && writer.write(0,31)
        && writer.write(1,1) && writer.write(1,6)
        && writer.write(1,1) && writer.write(1,5)
        && writer.write(1,1) && writer.write(0,31)
        && writer.write(1,2) && writer.write(2,3)
        && writer.write(1,1) && writer.write(0x811C9DC5U,32);
    return ok && writer.bit_count()-begin==641U;
}

}
