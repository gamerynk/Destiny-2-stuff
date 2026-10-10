#pragma once
#include "retained_authority_scope.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::native::forest_generator {
// exact reflected type37 authority 80805007 -> 80805008; a wire record, not a cast of
// native memory, since the four anchor groups overlap C++ alignment boundaries
inline constexpr std::uint32_t kSchema=0x80805007;
inline constexpr std::uint32_t kRuntimeClass=0x80804EF6,kSenseSchema=0x80805006;
inline constexpr std::size_t kRecipeBaseBits=475,kPayloadBaseBits=1750,kPayloadMaxBits=11350;
enum Override : std::uint8_t { Seed=1,Mode=2,Anchors=4,Enabled=8,Pick0=16,Pick1=32,Pick2=64 };
struct Anchor final {
    std::int8_t a{-1},b{};
    float weight{};
    bool active{true};
    friend constexpr bool operator==(const Anchor&,const Anchor&)=default;
};
struct Cell final {
    std::int16_t x{},y{},z{};
    friend constexpr bool operator==(const Cell&,const Cell&)=default;
};
struct Recipe final {
    std::uint32_t seed{};
    std::int8_t mode{};
    std::array<Anchor,4> anchors{};
    std::uint8_t overrides{};
    bool enabled{};
    float densityA{-1.0F},densityB{-1.0F};
    std::array<std::int32_t,5> scalarOverrides{-1,-1,-1,-1,-1};
    std::uint8_t blockedCount{};
    std::array<Cell,100> blockedCells{};
    friend constexpr bool operator==(const Recipe&,const Recipe&)=default;
};
struct State final {
    Recipe primary{},secondary{};
    std::uint32_t reportedSeed{};
    std::array<std::uint8_t,32> areas{};
    std::array<std::uint8_t,64> groups{};
    friend constexpr bool operator==(const State&,const State&)=default;
};
[[nodiscard]] inline bool valid(const Recipe& value) noexcept {
    if(value.overrides>127 || value.blockedCount>value.blockedCells.size()
        || !std::isfinite(value.densityA) || !std::isfinite(value.densityB))return false;
    for(const auto& anchor:value.anchors)if(!std::isfinite(anchor.weight))return false;
    return true;
}
[[nodiscard]] inline bool valid(const State& value) noexcept {
    return valid(value.primary) && valid(value.secondary);
}
[[nodiscard]] constexpr std::size_t body_bits(const State& value) noexcept {
    return kPayloadBaseBits+48U*(value.primary.blockedCount+value.secondary.blockedCount);
}
template<class Writer> [[nodiscard]] bool write_recipe(Writer& writer,const Recipe& value) noexcept {
    if(!valid(value) || !writer.write(value.seed,32)
        || !writer.write(static_cast<std::uint8_t>(value.mode)^0x80U,8))return false;
    for(const auto& anchor:value.anchors) {
        if(!writer.write(static_cast<std::uint8_t>(anchor.a)^0x80U,8)
            || !writer.write(static_cast<std::uint8_t>(anchor.b)^0x80U,8)
            || !writer.write(std::bit_cast<std::uint32_t>(anchor.weight),32)
            || !writer.write(anchor.active?1U:0U,1))return false;
    }
    if(!writer.write(value.overrides,7) || !writer.write(value.enabled?1U:0U,1)
        || !writer.write(std::bit_cast<std::uint32_t>(value.densityA),32)
        || !writer.write(std::bit_cast<std::uint32_t>(value.densityB),32))return false;
    for(const auto field:value.scalarOverrides)
        if(!writer.write(static_cast<std::uint32_t>(field)^0x80000000U,32))return false;
    if(!writer.write(value.blockedCount,7))return false;
    // native array capacity is 100, but its reflected parent param18=1 binds
    // serialization to the preceding count; unused cells are absent
    for(std::size_t index=0;index<value.blockedCount;++index) {
        const auto& cell=value.blockedCells[index];
        if(!writer.write(static_cast<std::uint16_t>(cell.x)^0x8000U,16)
            || !writer.write(static_cast<std::uint16_t>(cell.y)^0x8000U,16)
            || !writer.write(static_cast<std::uint16_t>(cell.z)^0x8000U,16))return false;
    }
    return true;
}
template<class Writer> [[nodiscard]] bool write_payload(Writer& writer,const State& value) noexcept {
    if(!valid(value) || !write_recipe(writer,value.primary) || !write_recipe(writer,value.secondary)
        || !writer.write(value.reportedSeed,32))return false;
    for(const auto field:value.areas)if(!writer.write(field,8))return false;
    for(const auto field:value.groups)if(!writer.write(field,8))return false;
    return true;
}
struct Request final {
    std::uint32_t registry{};
    std::uint16_t slot{};
    std::uint8_t bubble{};
    State state{};
};
struct Batch final {std::array<Request,4> entries{};std::size_t count{};};
[[nodiscard]] inline const Request* find(const Batch& batch,std::uint32_t registry,
    std::uint8_t type,std::uint16_t slot) noexcept {
    if(type!=37 || batch.count>batch.entries.size())return nullptr;
    for(std::size_t i=0;i<batch.count;++i)
        if(batch.entries[i].registry==registry && batch.entries[i].slot==slot)return &batch.entries[i];
    return nullptr;
}
template<class Roster> [[nodiscard]] bool valid(const Batch& batch,const Roster& roster,
    std::uint32_t region) noexcept {
    if(batch.count>batch.entries.size() || roster.groupCount>roster.groups.size())return false;
    for(std::size_t i=0;i<batch.count;++i) {
        const auto& request=batch.entries[i];
        if(!request.registry || request.registry==UINT32_MAX || request.registry==0x811C9DC5
            || request.slot>32767 || request.bubble>63 || !authority_scope::valid(roster,request.registry,request.bubble,region) || !valid(request.state))return false;
        for(std::size_t j=0;j<i;++j)
            if(batch.entries[j].registry==request.registry && batch.entries[j].slot==request.slot)return false;
        unsigned groups{},slots{};
        for(std::size_t g=0;g<roster.groupCount;++g) {
            const auto& group=roster.groups[g];if(group.key!=request.registry)continue;
            ++groups;
            if(group.slotTypes.size()!=group.slotIndices.size() || group.slotFlags.size()!=group.slotIndices.size())return false;
            for(std::size_t s=0;s<group.slotIndices.size();++s)if(group.slotIndices[s]==request.slot) {
                if(group.slotTypes[s]!=37 || group.slotFlags[s]!=3)return false;
                ++slots;
            }
        }
        if(groups!=1 || slots!=1)return false;
    }
    return true;
}
}
