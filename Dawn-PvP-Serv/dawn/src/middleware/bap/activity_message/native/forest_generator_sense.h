#pragma once
#include "forest_generator_authority.h"

namespace dawn::middleware::bap::activity_message::native::forest_generator_sense {
namespace wire=forest_generator;
inline constexpr std::uint32_t kSchema=0x80805006;
struct Progress final {
    std::uint64_t recipeHash{},openedGroups{};
    std::uint32_t primarySeed{},reportedSeed{},clearedAreas{};
    std::uint8_t primaryOverrides{};
    bool primaryEnabled{},canonicalFlags{};
    friend constexpr bool operator==(const Progress&,const Progress&)=default;
};
static_assert(sizeof(Progress)<=32);
struct Output final {Progress progress{};std::uint32_t revision{};bool root{};};
[[nodiscard]] inline std::uint64_t recipe_hash(const wire::State& state) noexcept {
    struct HashWriter final {
        std::uint64_t value{14695981039346656037ULL};
        bool write(std::uint64_t bits,std::size_t width) noexcept {
            for(std::size_t i=width;i>0;--i){value^=(bits>>(i-1))&1;value*=1099511628211ULL;}return true;
        }
    } writer;
    if(!wire::write_recipe(writer,state.primary) || !wire::write_recipe(writer,state.secondary))return 0;
    return writer.value;
}
[[nodiscard]] inline Progress summarize(const wire::State& state) noexcept {
    Progress out{recipe_hash(state),0,state.primary.seed,state.reportedSeed,0,state.primary.overrides,state.primary.enabled,true};
    for(std::size_t i=0;i<state.areas.size();++i) {
        out.canonicalFlags&=state.areas[i]<=1;
        if(state.areas[i])out.clearedAreas|=std::uint32_t{1}<<i;
    }
    for(std::size_t i=0;i<state.groups.size();++i) {
        out.canonicalFlags&=state.groups[i]<=1;
        if(state.groups[i])out.openedGroups|=std::uint64_t{1}<<i;
    }
    return out;
}

template<class Reader> [[nodiscard]] bool recipe(Reader& reader,wire::Recipe& out) noexcept {
    wire::Recipe r;std::uint64_t value{};
    if(!reader.read(32,value))return false;r.seed=static_cast<std::uint32_t>(value);
    if(!reader.read(8,value))return false;r.mode=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value^0x80U));
    for(auto& a:r.anchors) {
        if(!reader.read(8,value))return false;a.a=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value^0x80U));
        if(!reader.read(8,value))return false;a.b=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value^0x80U));
        if(!reader.read(32,value))return false;a.weight=std::bit_cast<float>(static_cast<std::uint32_t>(value));
        if(!reader.read(1,value))return false;a.active=value!=0;
    }
    if(!reader.read(7,value))return false;r.overrides=static_cast<std::uint8_t>(value);
    if(!reader.read(1,value))return false;r.enabled=value!=0;
    if(!reader.read(32,value))return false;r.densityA=std::bit_cast<float>(static_cast<std::uint32_t>(value));
    if(!reader.read(32,value))return false;r.densityB=std::bit_cast<float>(static_cast<std::uint32_t>(value));
    for(auto& scalar:r.scalarOverrides) {
        if(!reader.read(32,value))return false;scalar=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value^0x80000000U));
    }
    if(!reader.read(7,value) || value>r.blockedCells.size())return false;r.blockedCount=static_cast<std::uint8_t>(value);
    for(std::size_t i=0;i<r.blockedCount;++i) {
        auto& c=r.blockedCells[i];
        if(!reader.read(16,value))return false;c.x=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value^0x8000U));
        if(!reader.read(16,value))return false;c.y=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value^0x8000U));
        if(!reader.read(16,value))return false;c.z=std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value^0x8000U));
    }
    if(!wire::valid(r))return false;out=r;return true;
}
template<class Reader> [[nodiscard]] bool payload(Reader& reader,wire::State& out) noexcept {
    wire::State value;std::uint64_t field{};
    if(!recipe(reader,value.primary) || !recipe(reader,value.secondary) || !reader.read(32,field))return false;
    value.reportedSeed=static_cast<std::uint32_t>(field);
    for(auto& flag:value.areas){if(!reader.read(8,field))return false;flag=static_cast<std::uint8_t>(field);}
    for(auto& flag:value.groups){if(!reader.read(8,field))return false;flag=static_cast<std::uint8_t>(field);}
    out=value;return true;
}
// original 4D8490 adds root1 and revision32 around the reflected payload; this reader
// consumes no group terminator or guessed boundary
template<class Reader> [[nodiscard]] bool read(Reader& reader,Output& out,std::size_t& width) noexcept {
    const auto before=reader.remaining_bits();Output value;std::uint64_t field{};
    if(!reader.read(1,field))return false;value.root=field!=0;
    if(value.root) {
        wire::State state;
        if(!payload(reader,state))return false;
        value.progress=summarize(state);
    }
    if(!reader.read(32,field))return false;value.revision=static_cast<std::uint32_t>(field);
    width=before-reader.remaining_bits();out=value;return true;
}
}
