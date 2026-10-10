#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include "native_combatant_authority.h"
namespace dawn::state::activity::coo::native_atom {

inline constexpr std::uint8_t kSlotType=2;
inline constexpr std::uint32_t kComponentClass=0x8080834EU,kSenseSchema=0x80807DA2U,kAuthSchema=0x80807DA1U;
inline constexpr std::size_t kMinimumAuthBits=11,kMaximumAuthBits=8228;

// FNV sentinel of an unset registry key, shared by every absent nested ClientRef
inline constexpr std::uint32_t kAbsent=0x811C9DC5U;
// generations and revisions are 31-bit counters, accepted by the client only while positive
inline constexpr std::uint32_t kMaximumCounter=0x7FFFFFFFU;
// resume lane and lane count are six bits each; the client runner holds 32 lanes
inline constexpr std::size_t kAtomCapacity=32;
inline constexpr std::uint8_t kMaximumSeed=0x3FU;

// lane ClientRef is 55 bits: 32-bit key, 7-bit type biased by one, 16-bit index biased 32768
struct Ref final { std::uint32_t registry{kAbsent}; std::int8_t type{-1}; std::int16_t index{-1}; };

// zero-based tag of the ten primary lane schemas; the wire tag is this plus one.
// Order must match the host's Type2LanePrimary variant order, which selects the child schema
enum class Selector : std::uint8_t { snapTo,sequence,sleep,moveTo,trivial,controlFlag,
    setTemperament,setChannel,face,ability };
// snapTo and face have identical payloads, so swapping them passes every width check and only
// shows up in game. Pin both tags to the host's variant indices
static_assert(static_cast<std::uint8_t>(Selector::snapTo)==0
    && static_cast<std::uint8_t>(Selector::face)==8);
inline constexpr std::array<std::size_t,10> kSelectorBits{63,32,32,64,0,6,33,64,63,162};

inline constexpr std::size_t kProgramBlockBits=43;
inline constexpr std::size_t kControlRootBits=149;
inline constexpr std::size_t kSpawnRootBits=42;
static_assert(native_combatant::kBindBits==kSpawnRootBits);
inline constexpr std::size_t kMaximumLaneBits=6U+162U+11U;
inline constexpr std::size_t kMaximumProgramBits=
    kControlRootBits+kProgramBlockBits+kAtomCapacity*(1U+kMaximumLaneBits);
inline constexpr std::size_t kMaximumProgramBytes=(kMaximumProgramBits+7U)/8U;
static_assert(kMaximumLaneBits==179 && kMaximumProgramBits==5952
    && kMaximumProgramBits<=kMaximumAuthBits);

// wire 1 (logical 0) is the type-2-owned creation path and belongs only to the spawn root;
// a program binding an authored squad member writes wire 2
enum class Binding : std::uint8_t { selfOwned=0,squadMember=2 };

struct Atom final {
    Selector selector{Selector::trivial};
    Ref target{};
    std::uint32_t value{};
    float real{};
    std::array<std::uint32_t,3> values{};
    std::int8_t mode{-1};
    std::int8_t marker{-1};
    bool enabled{};
    std::int16_t quantized{-1};
};

[[nodiscard]] constexpr bool finite(float value) noexcept {
    return (std::bit_cast<std::uint32_t>(value)&0x7F800000U)!=0x7F800000U;
}
[[nodiscard]] constexpr bool valid(const Ref& ref) noexcept { return ref.type>=-1 && ref.type<=126; }
[[nodiscard]] constexpr bool valid(const Atom& atom) noexcept {
    if(static_cast<std::size_t>(atom.selector)>=kSelectorBits.size() || atom.quantized>0x7FF) { return false; }
    switch(atom.selector) {
    case Selector::snapTo: case Selector::face: case Selector::moveTo:
        return valid(atom.target) && atom.value<=0xFFU;
    case Selector::sleep: case Selector::setChannel: return finite(atom.real);
    case Selector::controlFlag: return atom.value<=0x3FU;
    case Selector::ability: return valid(atom.target) && atom.mode>=-1 && atom.mode<=6;
    default: return true;
    }
}
[[nodiscard]] constexpr std::size_t lane_bits(const Atom& atom) noexcept {
    return 6U+kSelectorBits[static_cast<std::size_t>(atom.selector)]+(atom.quantized<0?0U:11U);
}

// native selector 0 applies the authored point's whole transform at once (client A9C140)
[[nodiscard]] constexpr Atom snap_to(const Ref& target,std::uint8_t value=0) noexcept {
    Atom atom{}; atom.selector=Selector::snapTo; atom.target=target; atom.value=value; return atom;
}
[[nodiscard]] constexpr Atom move_to(const Ref& target,std::uint8_t value,bool enabled) noexcept {
    Atom atom{}; atom.selector=Selector::moveTo; atom.target=target; atom.value=value;
    atom.enabled=enabled; return atom;
}
// native selector 8 turns toward the path direction and keeps position (client A9BD70)
[[nodiscard]] constexpr Atom face(const Ref& target,std::uint8_t value=0) noexcept {
    Atom atom{}; atom.selector=Selector::face; atom.target=target; atom.value=value; return atom;
}
[[nodiscard]] constexpr Atom sequence(std::uint32_t value) noexcept {
    Atom atom{}; atom.selector=Selector::sequence; atom.value=value; return atom;
}
[[nodiscard]] constexpr Atom sleep(float seconds) noexcept {
    Atom atom{}; atom.selector=Selector::sleep; atom.real=seconds; return atom;
}
[[nodiscard]] constexpr Atom set_channel(std::uint32_t channel,float value) noexcept {
    Atom atom{}; atom.selector=Selector::setChannel; atom.value=channel; atom.real=value; return atom;
}
[[nodiscard]] constexpr Atom set_temperament(std::uint32_t identity,bool enabled) noexcept {
    Atom atom{}; atom.selector=Selector::setTemperament; atom.value=identity; atom.enabled=enabled;
    return atom;
}
[[nodiscard]] constexpr Atom control_flag(std::uint8_t value) noexcept {
    Atom atom{}; atom.selector=Selector::controlFlag; atom.value=value; return atom;
}
[[nodiscard]] constexpr Atom trivial() noexcept { return Atom{}; }
[[nodiscard]] constexpr Atom ability(std::uint32_t group,std::uint32_t name) noexcept {
    Atom atom{}; atom.selector=Selector::ability; atom.values={group,name,kAbsent}; return atom;
}
[[nodiscard]] constexpr Atom ability_at(std::uint32_t group,std::uint32_t name,const Ref& target,
    std::int8_t mode=0,std::int8_t marker=-1) noexcept {
    Atom atom{}; atom.selector=Selector::ability; atom.values={group,name,kAbsent};
    atom.target=target; atom.mode=mode; atom.marker=marker; return atom;
}

struct Program final {
    std::array<Atom,kAtomCapacity> atoms{};
    std::uint32_t generation{};
    std::uint32_t revision{};
    std::uint8_t count{};
    std::uint8_t progressSeed{};
    Binding binding{Binding::selfOwned};
    bool spawn{};
};

template<class Writer>[[nodiscard]] bool write_atom(Writer& writer,const Atom& atom) noexcept {
    if(!valid(atom)) { return false; }
    const auto begin=writer.bit_count();
    const auto reference=[&writer](const Ref& ref) noexcept {
        return writer.write(ref.registry,32)
            && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(ref.type)+1),7)
            && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(ref.index)+32768),16);
    };
    bool ok=writer.write(1,1)
        && writer.write(static_cast<std::uint32_t>(atom.selector)+1U,4)
        && writer.write(atom.quantized<0?1U:3U,2);
    switch(atom.selector) {
    case Selector::snapTo: case Selector::face:
        ok=ok && reference(atom.target) && writer.write(atom.value,8); break;
    case Selector::moveTo:
        ok=ok && reference(atom.target) && writer.write(atom.value,8)
            && writer.write(atom.enabled?1U:0U,1); break;
    case Selector::sequence: ok=ok && writer.write(atom.value,32); break;
    case Selector::sleep: ok=ok && writer.write(std::bit_cast<std::uint32_t>(atom.real),32); break;
    case Selector::trivial: break;
    case Selector::controlFlag: ok=ok && writer.write(atom.value,6); break;
    case Selector::setTemperament:
        ok=ok && writer.write(atom.value,32) && writer.write(atom.enabled?1U:0U,1); break;
    case Selector::setChannel:
        ok=ok && writer.write(atom.value,32)
            && writer.write(std::bit_cast<std::uint32_t>(atom.real),32); break;
    case Selector::ability:
        for(const auto identity:atom.values) { ok=ok && writer.write(identity,32); }
        ok=ok && reference(atom.target)
            && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(atom.mode)+1),3)
            && writer.write(static_cast<std::uint32_t>(static_cast<std::int32_t>(atom.marker)+128),8);
        break;
    default: return false;
    }
    if(ok && atom.quantized>=0) { ok=writer.write(static_cast<std::uint32_t>(atom.quantized),11); }
    return ok && writer.bit_count()-begin==1U+lane_bits(atom);
}

template<class Writer>[[nodiscard]] bool write_program(Writer& writer,const Program& program,
    std::span<const Ref> cargo={},std::uint32_t deliveryRevision=0) noexcept {
    if(program.count==0 || program.count>kAtomCapacity
        || program.generation==0 || program.generation>kMaximumCounter
        || program.revision==0 || program.revision>kMaximumCounter
        || program.progressSeed>kMaximumSeed || program.progressSeed>program.count
        || (program.spawn && program.binding!=Binding::selfOwned)
        || cargo.size()>8 || (cargo.empty()!=(deliveryRevision==0))
        || deliveryRevision>kMaximumCounter) { return false; }
    for(std::size_t index=0;index<cargo.size();++index) {
        if(cargo[index].registry==0 || cargo[index].registry==kAbsent
            || cargo[index].type!=1 || cargo[index].index<0) { return false; }
        for(std::size_t prior=0;prior<index;++prior) {
            if(cargo[index].registry==cargo[prior].registry
                && cargo[index].index==cargo[prior].index) { return false; }
        }
    }
    std::size_t lanes=0;
    for(std::size_t index=0;index<program.count;++index) {
        if(!valid(program.atoms[index])) { return false; }
        lanes+=1U+lane_bits(program.atoms[index]);
    }
    const auto begin=writer.bit_count();
    bool ok=false;
    if(program.spawn) {
        ok=writer.write(1,1) && writer.write(program.generation,31)
            && writer.write(1,2) && writer.write(1,3) && writer.write(1,1)
            && writer.write(0,1) && writer.write(0,1);
    } else {
        ok=writer.write(0,1) && writer.write(0,2)
            && writer.write(static_cast<std::uint32_t>(program.binding),3)
            && writer.write(1,1) && writer.write(0,1)
            && writer.write(1,1) && writer.write(program.generation,31)
            && writer.write(0,6) && writer.write(0,6) && writer.write(0,3)
            && writer.write(kAbsent,32) && writer.write(0,7) && writer.write(32767U,16)
            && writer.write(0,32) && writer.write(0,5);
    }
    ok=ok && writer.write(1,1) && writer.write(program.revision,31)
        && writer.write(program.progressSeed,6) && writer.write(program.count,6);
    for(std::size_t index=0;ok && index<program.count;++index) {
        ok=write_atom(writer,program.atoms[index]);
    }
    // a replacement body keeps both fields: clearing .6 while publishing .7 cancels the path
    ok=ok && writer.write(cargo.empty()?0U:1U,1);
    if(!cargo.empty()) {
        ok=ok && writer.write(cargo.size(),4);
        for(const auto& squad:cargo) {
            ok=ok && writer.write(squad.registry,32) && writer.write(2,7)
                && writer.write(static_cast<std::uint32_t>(squad.index)+32768U,16);
        }
        ok=ok && writer.write(deliveryRevision,31);
    }
    return ok
        && writer.bit_count()-begin
            ==(program.spawn?kSpawnRootBits:kControlRootBits)+kProgramBlockBits+lanes
                +(cargo.empty()?0U:35U+55U*cargo.size());
}

template<class Writer>[[nodiscard]] bool write_retirement(Writer& writer,
    std::uint32_t generation) noexcept {
    if(generation==0 || generation>kMaximumCounter) { return false; }
    return writer.write(1,1) && writer.write(generation,31) && writer.write(1,2)
        && writer.write(1,3) && writer.write(0,1) && writer.write(0,1)
        && writer.write(0,1) && writer.write(0,1) && writer.write(1,1)
        && writer.write(0,4) && writer.write(generation,31);
}

inline constexpr std::uint8_t kChannelSlotType=24;
inline constexpr std::uint32_t kChannelComponentClass=0x80804F3BU,kChannelSenseSchema=0x80804F3DU,
    kChannelAuthSchema=0x80804F40U;
inline constexpr std::size_t kChannelCapacity=4;
inline constexpr std::size_t kMaximumChannelBits=3U+96U*kChannelCapacity;
inline constexpr std::size_t kMaximumChannelBytes=(kMaximumChannelBits+7U)/8U;
static_assert(kMaximumChannelBits==387);
inline constexpr float kMinimumChannelValue=-100.F,kMaximumChannelValue=100.F;

template<class Writer>[[nodiscard]] bool write_channels(Writer& writer,std::span<const float> values,
    std::int32_t revision,float blend=0.F) noexcept {
    if(values.empty() || values.size()>kChannelCapacity || revision<0
        || !finite(blend) || blend<0.F) { return false; }
    for(const auto value:values) {
        if(!finite(value) || value<kMinimumChannelValue || value>kMaximumChannelValue) { return false; }
    }
    const auto begin=writer.bit_count();
    bool ok=writer.write(static_cast<std::uint32_t>(values.size()),3);
    for(const auto value:values) {
        ok=ok && writer.write(static_cast<std::uint32_t>(revision)+0x80000000U,32)
            && writer.write(std::bit_cast<std::uint32_t>(value),32)
            && writer.write(std::bit_cast<std::uint32_t>(blend),32);
    }
    return ok && writer.bit_count()-begin==3U+96U*values.size();
}
[[nodiscard]] constexpr std::size_t channel_bits(std::size_t count) noexcept { return 3U+96U*count; }

// an empty action group fails the native lookup; it is not a wildcard
inline constexpr std::uint32_t kThresherActionGroup=0x07EBF354U,kThresherEnter=0x4482A76DU,
    kThresherExit=0x7D0D39A9U;
inline constexpr std::uint32_t kThresherDoors=0x80296344U;
inline constexpr std::uint32_t kLedgeRegistry=0xA5F083B5U;
inline constexpr std::uint16_t kThresherPilotSlot=21;
inline constexpr Ref kThresherEntry{kLedgeRegistry,58,295};
inline constexpr Ref kThresherExitPath{kLedgeRegistry,58,296};

[[nodiscard]] constexpr Program thresher_arrival() noexcept {
    Program program{};
    program.atoms[0]=set_channel(kThresherDoors,0.F);
    program.atoms[1]=snap_to(kThresherEntry,0);
    program.atoms[2]=ability(kThresherActionGroup,kThresherEnter);
    program.atoms[3]=set_channel(kThresherDoors,1.F);
    program.count=4;
    program.generation=1;
    program.revision=1;
    program.spawn=true;
    return program;
}

[[nodiscard]] constexpr Program thresher_departure() noexcept {
    Program program{};
    program.atoms[0]=set_channel(kThresherDoors,0.F);
    program.atoms[1]=move_to(kThresherExitPath,1,true);
    program.atoms[2]=ability(kThresherActionGroup,kThresherExit);
    program.count=3;
    program.generation=1;
    program.revision=2;
    return program;
}

inline constexpr std::uint32_t kThresherSpawnRevision=1;
inline constexpr std::uint32_t kArrivalRevision=1,kDepartureRevision=2;
inline constexpr std::uint32_t kArrivalCompleteState=4,kDepartureCompleteState=3;
inline constexpr std::uint32_t kThresherRetireGeneration=2;

inline constexpr float kLaserOn=100.F,kLaserOff=0.F;
inline constexpr std::uint32_t kLaserHighMs=2500+7000,kLaserLowMs=1500+2000;
inline constexpr std::size_t kLaserRooms=3,kLaserBanks=2;
struct LaserBank final { std::uint16_t first,count; };
inline constexpr std::array<std::array<LaserBank,kLaserBanks>,kLaserRooms> kLaserBankSlots{{
    {{{189,5},{194,5}}},
    {{{199,5},{204,5}}},
    {{{209,10},{219,10}}},
}};

} // namespace dawn::state::activity::coo::native_atom
