#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace dawn::state::activity::coo::native_combatant {

// task row -1 means cost this squad but select nothing yet. A nonzero revision is echoed back
// with the reported costs; zero means reports carry no revision
struct TacticalGroup final {
    std::uint32_t registry{};
    std::uint16_t slot{};
    std::int8_t row{-1};
    std::uint32_t revision{};
};

struct Source final {
    std::uint32_t registry{};
    std::uint32_t generation{};
    std::uint16_t ruleSlot{};
    std::uint32_t looseRequested{};
    TacticalGroup tactical{};
    std::uint32_t secondRequested{};
    bool hasSecondCategory{};
    bool hasRule{true};
    bool memberOwned{};
    // on a changed +7C generation native 4E9550 uses BC=0 to remove the old source-owned
    // entities; normal loose sources keep BC=1
    bool retireOwned{};
    bool sceneRequested{};
    std::uint8_t variant{};
};
inline constexpr std::size_t kSourceBits = 641;
inline constexpr std::size_t kTwoCategorySourceBits = 673;

[[nodiscard]] constexpr std::size_t authored_source_bits(std::size_t categories,bool objective) noexcept {
    return 214U+32U*categories+(objective?91U:0U);
}

template<class Writer>
[[nodiscard]] bool write_authored_source(Writer& writer,const Source& source,
                                         const std::array<std::int8_t,4>& profile) noexcept {
    const auto& task=source.tactical;
    const bool objective=task.registry!=0;
    if(source.registry==0 || source.registry==0x811C9DC5U || source.hasRule || source.memberOwned
        || source.generation==0 || source.generation>0x7FFFFFFFU
        || source.variant>5
        || (!source.hasSecondCategory && source.secondRequested!=0)
        || source.looseRequested>unsigned(INT32_MAX) || source.secondRequested>unsigned(INT32_MAX)
        || (objective ? (task.registry==0x811C9DC5U || task.slot>0x7FFFU || task.row<-1
                         || task.row>=24 || task.revision==0 || task.revision>0x7FFFFFFFU)
                      : (task.row!=-1 || task.slot!=0 || task.revision!=0))) { return false; }
    constexpr std::array<std::uint8_t,4> widths{2,3,2,3};
    for(std::size_t i=0;i<profile.size();++i) {
        if(profile[i]<0 || unsigned(profile[i])>=(1U<<widths[i])-1U) { return false; }
    }
    const auto begin=writer.bit_count();
    const auto unset=[&writer]() noexcept {
        return writer.write(1,1) && writer.write(0x811C9DC5U,32)
            && writer.write(0,7) && writer.write(32767U,16);
    };
    if(!writer.write(objective?1U:0U,1)
        || (objective && !(writer.write(task.registry,32) && writer.write(4,7)
                            && writer.write(32768U+task.slot,16)))
        || !writer.write(0,2) || !writer.write(1,1)
        || !writer.write(source.hasSecondCategory?2U:1U,4)
        || !writer.write(0x80000000U+source.looseRequested,32)
        || (source.hasSecondCategory && !writer.write(0x80000000U+source.secondRequested,32))
        || !writer.write(0,1) || !writer.write(1,1)
        || !writer.write(static_cast<std::uint32_t>(source.variant)+1U,3)) { return false; }
    for(std::size_t i=0;i<profile.size();++i) {
        if(!writer.write(static_cast<unsigned>(profile[i])+1U,widths[i])) { return false; }
    }
    const bool ok=writer.write(1,1) && writer.write(source.generation,31)
        && writer.write(0,4) && unset() && unset()
        && writer.write(objective?1U:0U,1) && (!objective || writer.write(task.revision,31))
        && writer.write(0,2) && writer.write(objective?1U:0U,1)
        && (!objective || writer.write(static_cast<unsigned>(static_cast<int>(task.row)+1),5))
        && writer.write(0,1) && writer.write(2,2) && writer.write(source.sceneRequested?2U:3U,3)
        && writer.write(1,1) && writer.write(0x811C9DC5U,32);
    return ok && writer.bit_count()-begin==authored_source_bits(source.hasSecondCategory?2U:1U,objective);
}

template<class Writer>
[[nodiscard]] bool write_source(Writer& writer,const Source& source) noexcept {
    if(source.registry==0 || source.registry==0x811C9DC5U
        || source.generation==0 || source.generation>0x7FFFFFFFU
        || source.variant>5
        || (source.hasRule && source.ruleSlot>0x7FFFU) || source.looseRequested>unsigned(INT32_MAX)
        || source.secondRequested>unsigned(INT32_MAX)
        || (!source.hasSecondCategory && source.secondRequested!=0)
        || static_cast<unsigned>(source.looseRequested)+source.secondRequested>unsigned(INT32_MAX)) { return false; }
    if(source.retireOwned && (source.looseRequested || source.secondRequested || source.memberOwned)) {return false;}
    const auto& tactical=source.tactical;
    const bool objective=tactical.registry!=0;
    if(objective ? (tactical.registry==0x811C9DC5U || tactical.slot>0x7FFFU
                    || tactical.row<-1 || tactical.row>=24 || tactical.revision>0x7FFFFFFFU)
                 : (tactical.row!=-1 || tactical.slot!=0 || tactical.revision!=0)) { return false; }
    if(source.memberOwned && (source.looseRequested || source.secondRequested || objective)) { return false; }
    const auto begin=writer.bit_count();
    const auto absent=[&writer]() noexcept {
        return writer.write(1,1) && writer.write(0x811C9DC5U,32)
            && writer.write(0,7) && writer.write(32767U,16);
    };
    const bool ok=(objective
        ? (writer.write(1,1) && writer.write(tactical.registry,32)
            && writer.write(4,7) && writer.write(32768U+tactical.slot,16))
        : absent()) && absent()
        && writer.write(1,1) && writer.write(0,3)
        && writer.write(1,1) && writer.write(source.hasSecondCategory?2U:1U,4)
        && writer.write(0x80000000U+source.looseRequested,32)
        && (!source.hasSecondCategory || writer.write(0x80000000U+source.secondRequested,32))
        && writer.write(1,1) && writer.write(0,4)
        // bias one maps variants 0..5 to wire 1..6; wire 0 is -1 and would index before the
        // six-template array
        && writer.write(1,1)
        && writer.write(static_cast<std::uint32_t>(source.variant)+1U,3)
        && writer.write(1,2) && writer.write(0,8)
        && writer.write(1,1) && writer.write(source.generation,31)
        && writer.write(1,1) && writer.write(0,32)
        && writer.write(1,1) && writer.write(source.memberOwned?0x811C9DC5U:0U,32)
        && absent() && absent()
        && (source.hasRule ? (writer.write(1,1) && writer.write(source.registry,32)
            && writer.write(67,7) && writer.write(32768U+source.ruleSlot,16)) : absent())
        && absent()
        && writer.write(1,1) && writer.write(objective?tactical.revision:0U,31)
        && writer.write(1,1) && writer.write(0,31)
        && writer.write(1,1) && writer.write(source.memberOwned?1U:0U,6)
        && writer.write(1,1) && writer.write(source.memberOwned?1U
            :static_cast<std::uint32_t>(static_cast<std::int32_t>(tactical.row)+1),5)
        && writer.write(1,1) && writer.write(source.generation,31)
        && writer.write(source.memberOwned || source.retireOwned?1U:2U,2) && writer.write(source.sceneRequested?2U:1U,3)
        && writer.write(1,1) && writer.write(0x811C9DC5U,32);
    return ok && writer.bit_count()-begin==(source.hasSecondCategory?kTwoCategorySourceBits:kSourceBits);
}

// type-2 squad-member ownership root. Member selection binds it only when .3 is true, the
// actor handle is unset and .2 is 1 or 3; wire 1 in .2 spawns a second actor instead
inline constexpr std::size_t kBindBits=42;
template<class Writer>
[[nodiscard]] bool write_spawn(Writer& writer,std::uint32_t generation) noexcept {
    return generation!=0 && generation<=0x7FFFFFFFU
        && writer.write(1,1) && writer.write(generation,31)
        && writer.write(0,2) && writer.write(1,3) && writer.write(1,1)
        && writer.write(0,1) && writer.write(0,1) && writer.write(0,1) && writer.write(0,1);
}
template<class Writer>
[[nodiscard]] bool write_bind(Writer& writer,std::uint32_t generation) noexcept {
    if(generation==0 || generation>0x7FFFFFFFU) { return false; }
    const auto begin=writer.bit_count();
    const bool ok=writer.write(1,1) && writer.write(generation,31)
        && writer.write(0,2) && writer.write(2,3) && writer.write(1,1)
        && writer.write(0,1) && writer.write(0,1) && writer.write(0,1) && writer.write(0,1);
    return ok && writer.bit_count()-begin==kBindBits;
}

// AB71E0 consumes logical .1=0 to detach and destroy the actor; .3=false blocks reattaching.
// Source retirement alone skips actors already owned by a named member
template<class Writer>
[[nodiscard]] bool write_retire_member(Writer& writer,std::uint32_t generation) noexcept {
    if(generation==0 || generation>0x7FFFFFFFU) { return false; }
    const auto begin=writer.bit_count();
    const bool ok=writer.write(1,1) && writer.write(generation,31)
        && writer.write(1,2) && writer.write(2,3) && writer.write(0,1)
        && writer.write(0,1) && writer.write(0,1) && writer.write(0,1) && writer.write(0,1);
    return ok && writer.bit_count()-begin==kBindBits;
}

} // namespace dawn::state::activity::coo::native_combatant
