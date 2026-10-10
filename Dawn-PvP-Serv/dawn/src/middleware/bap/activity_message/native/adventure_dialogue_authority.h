#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace dawn::middleware::bap::activity_message::native::dialogue {
// native 80804F77 authority; the bank row selects the authored conversation graph and this
// record never substitutes individual voice/media or speaker identities
inline constexpr std::size_t kRows=128,kDecodedBytes=0x1008,kInactiveBits=19767;
inline constexpr std::uint8_t kNoRow=128;
inline constexpr std::uint32_t kAbsent=0x811C9DC5U;
// payload ownership is shared by mission publishers and routed activity requests; row
// retirement must explicitly clear optional times when reusing native state
struct Rows final {
    std::span<const std::uint32_t> generations{};
    std::uint8_t activeRow{kNoRow};
    bool clearInactiveTimes{};
};
[[nodiscard]] constexpr bool valid(const Rows& r) noexcept {
    if(r.generations.empty() || r.generations.size()>kRows
        || (r.activeRow!=kNoRow && (r.activeRow>=r.generations.size() || !r.generations[r.activeRow])))return false;
    return true;
}
[[nodiscard]] constexpr std::size_t body_bits(const Rows& r) noexcept {
    if(!valid(r))return 0;
    std::size_t count=r.activeRow==kNoRow?0:1;
    if(r.clearInactiveTimes)for(std::size_t i=0;i<r.generations.size();++i)if(i!=r.activeRow && r.generations[i])++count;
    return kInactiveBits+64*count;
}
struct Request final {
    std::uint32_t registry{};
    std::uint16_t slot{};
    std::uint8_t bankRows{},activeRow{kNoRow};
    std::array<std::uint32_t,kRows> generations{};
    std::uint32_t scope{UINT32_MAX};
    // when advancing consumed rows, zero historical optional times explicitly; omitting an
    // optional scalar can retain its previous decoded value
    bool clearInactiveTimes{};
    friend bool operator==(const Request&,const Request&)=default;
};
struct Batch final {std::array<Request,4> entries{};std::size_t count{};};
[[nodiscard]] constexpr bool valid(const Request& r) noexcept {
    if(!r.registry || r.registry==UINT32_MAX || r.registry==kAbsent || r.slot>32767
        || !r.bankRows || r.bankRows>kRows || (r.activeRow!=kNoRow && r.activeRow>=r.bankRows)
        || (r.scope!=UINT32_MAX && r.scope>63))return false;
    if(!valid(Rows{std::span{r.generations}.first(r.bankRows),r.activeRow,r.clearInactiveTimes}))return false;
    for(std::size_t i=r.bankRows;i<kRows;++i)if(r.generations[i])return false;
    return true;
}
[[nodiscard]] constexpr std::size_t body_bits(const Request& r) noexcept {
    return valid(r)?body_bits(Rows{std::span{r.generations}.first(r.bankRows),r.activeRow,r.clearInactiveTimes}):0;
}
[[nodiscard]] inline const Request* find(const Batch& b,std::uint32_t key,std::uint8_t type,std::uint16_t slot) noexcept {
    if(type!=53 || b.count>b.entries.size())return nullptr;
    for(std::size_t i=0;i<b.count;++i)if(b.entries[i].registry==key && b.entries[i].slot==slot)return &b.entries[i];
    return nullptr;
}
template<class Roster> [[nodiscard]] bool valid(const Batch& b,const Roster& roster) noexcept {
    if(b.count>b.entries.size() || roster.groupCount>roster.groups.size() || roster.topLevelGroupCount>roster.groupCount)return false;
    for(std::size_t i=0;i<b.count;++i) {
        const auto& r=b.entries[i];if(!valid(r))return false;
        for(std::size_t j=0;j<i;++j)if(b.entries[j].registry==r.registry && b.entries[j].slot==r.slot)return false;
        unsigned groups{},slots{};
        for(std::size_t g=0;g<roster.groupCount;++g) {
            const auto& group=roster.groups[g];if(group.key!=r.registry)continue;
            if((r.scope==UINT32_MAX ? g>=roster.topLevelGroupCount : g<roster.topLevelGroupCount)
                || group.slotTypes.size()!=group.slotIndices.size()
                || group.slotFlags.size()!=group.slotIndices.size())return false;
            ++groups;
            for(std::size_t s=0;s<group.slotIndices.size();++s)if(group.slotIndices[s]==r.slot) {
                if(group.slotTypes[s]!=53 || (group.slotFlags[s]&2)==0)return false;
                ++slots;
            }
        }
        unsigned admissions{};
        for(const auto& block:roster.bubbleSubBlocks)
            for(std::size_t k=0;k<block.keys.size();++k)if(block.keys[k]==r.registry) {
                if(r.scope==UINT32_MAX || block.bubble!=r.scope
                    || (!block.presence.empty() && (block.presence.size()!=block.keys.size() || block.presence[k]!=1)))return false;
                ++admissions;
            }
        if(groups!=1 || slots!=1 || (r.scope!=UINT32_MAX && admissions!=1))return false;
    }
    return true;
}
template<class Writer> [[nodiscard]] bool write(Writer& w,const Rows& r) noexcept {
    if(!valid(r))return false;
    const auto absent=[&] {return w.write(kAbsent,32) && w.write(0,7) && w.write(32767,16);};
    if(!absent())return false;
    for(std::size_t i=0;i<kRows;++i) {
        const auto generation=i<r.generations.size()?r.generations[i]:0U;
        const bool active=i==r.activeRow;
        const bool timePresent=active || (r.clearInactiveTimes && generation);
        // the native time predicate accepts a nonzero optional time; mode2 owns expiry.
        // Retired rows keep their consumed generation
        if(!w.write(UINT64_MAX,64) || !w.write(timePresent?1U:0U,1)
            || (timePresent && !w.write(active?1U:0U,64)) || !absent()
            || !w.write(static_cast<std::uint64_t>(generation)+0x80000000ULL,32)
            || !w.write(active?3U:1U,2))return false;
    }
    return true;
}
template<class Writer> [[nodiscard]] bool write(Writer& w,const Request& r) noexcept {
    return valid(r) && write(w,Rows{std::span{r.generations}.first(r.bankRows),r.activeRow,r.clearInactiveTimes});
}
using Decoded=std::array<std::byte,kDecodedBytes>;
[[nodiscard]] inline Decoded decoded(const Request& r) noexcept {
    Decoded out{};if(!valid(r))return out;
    const auto put=[&](std::size_t at,auto value) {std::memcpy(out.data()+at,&value,sizeof(value));};
    const auto absent=[&](std::size_t at) {put(at,kAbsent);out[at+4]=std::byte{255};out[at+6]=out[at+7]=std::byte{255};};
    absent(0);
    for(std::size_t i=0;i<kRows;++i) {
        const auto at=8+i*32;put(at,UINT64_MAX);put(at+8,std::uint64_t{i==r.activeRow?1U:0U});absent(at+16);
        put(at+24,r.generations[i]);out[at+28]=i==r.activeRow?std::byte{2}:std::byte{};
    }
    return out;
}
[[nodiscard]] constexpr bool state_field(std::size_t i) noexcept {
    if(i>=kDecodedBytes)return false;
    if(i<8)return i!=5;
    const auto at=(i-8)%32;return at<=28 && at!=21;
}
[[nodiscard]] constexpr bool decoder_writes(std::size_t i,const Request& r) noexcept {
    if(!state_field(i))return false;
    if(i>=8 && (i-8)%32>=8 && (i-8)%32<16)
        return (i-8)/32==r.activeRow || (r.clearInactiveTimes && r.generations[(i-8)/32]);
    return true;
}
[[nodiscard]] inline bool matches_fields(std::span<const std::byte> bytes,const Request& r) noexcept {
    if(bytes.size()!=kDecodedBytes || !valid(r))return false;
    const auto expected=decoded(r);
    for(std::size_t i=0;i<bytes.size();++i)if(state_field(i) && bytes[i]!=expected[i])return false;
    return true;
}
}
