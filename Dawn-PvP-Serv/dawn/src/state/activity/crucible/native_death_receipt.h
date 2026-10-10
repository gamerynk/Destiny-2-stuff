#pragma once
#include <cstdint>
#include <span>
#include "../../../middleware/encoding/bit_reader.h"

namespace dawn::state::activity::crucible::death_receipt {
// PC 86657 activity transport uses the game's type-1 override (0x80806447),
// not the generic engine fallback 0x808087DC
inline constexpr std::uint32_t kTarget = 1412;
struct Actor { std::uint64_t player{}, account{}, character{}; };
struct Receipt {
    std::uint32_t sequence{};
    std::uint64_t owner{};
    Actor killer{}, victim{};
};
namespace detail {
using Reader = middleware::encoding::bits::Reader;
inline bool optional(Reader& r, std::uint8_t width, std::uint64_t& value) noexcept {
    std::uint64_t present{}; value=0;
    return r.read(1,present) && (!present || r.read(width,value));
}
inline bool optional(Reader& r, std::uint8_t width) noexcept {
    std::uint64_t ignored{}; return optional(r,width,ignored);
}
inline bool actor(Reader& r, Actor& out) noexcept {
    return r.skip(3) && optional(r,32) && r.skip(3+2+6)
        && optional(r,32) && optional(r,32) && optional(r,64,out.player)
        && optional(r,64,out.account) && optional(r,64,out.character) && optional(r,32);
}
}
inline bool decode(std::span<const std::byte> bytes, Receipt& result) noexcept {
    result={}; Receipt v{}; detail::Reader r(bytes); std::uint64_t n{}, kind{};
    if (!r.read(32,n)) return false;
    v.sequence=static_cast<std::uint32_t>(n);
    if (!r.read(3,kind) || kind!=2 || !r.read(64,v.owner) || !v.owner
        || !detail::optional(r,5) || !detail::optional(r,7) || !r.skip(96)
        || !detail::optional(r,11) || !detail::optional(r,32) || !r.skip(11+32+32+3)
        || !detail::optional(r,11) || !r.skip(1+3*64)
        || !detail::actor(r,v.killer) || !detail::actor(r,v.victim)
        || !r.read(4,n) || n>9) return false;
    if (!r.skip(static_cast<std::size_t>(n)*(3+64+6+64+64+32+1+3+2+8))
        || !r.skip(32)) return false;
    for (unsigned i=0;i<3;++i) {
        if (!r.read(4,n) || n>15 || !r.skip(static_cast<std::size_t>(n)*32)) return false;
    }
    if (!r.skip(64) || r.remaining_bits()>7) return false;
    if (!r.read(static_cast<std::uint8_t>(r.remaining_bits()),n) || n) return false;
    // only a complete player death can enter the referee; environment deaths are decoded but
    // give no opposing credit without both identities
    if (!v.killer.player || !v.killer.account || v.killer.character!=v.killer.player
        || !v.victim.player || !v.victim.account || v.victim.character!=v.victim.player
        || v.owner!=v.victim.player) return false;
    result=v; return true;
}
}
