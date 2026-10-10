#pragma once
#include "../../state/activity/crucible/match_host.h"

namespace dawn::middleware::crucible::match_view_wire {
using View=state::activity::crucible::host::View;
inline constexpr std::size_t kHeader=120,kRow=93,kMaximum=kHeader+12*kRow;
inline bool valid(const View& v) noexcept {
    using namespace state::activity::crucible;
    if(v.count>12 || v.phase>host::Phase::results
        || (v.mode!=match_rules::Mode::clash && v.mode!=match_rules::Mode::rumble)
        || (v.winner!=255 && v.winner>=12) || v.remainingMs>v.durationMs
        || v.durationMs>86'400'000 || v.elapsedMs>v.durationMs) return false;
    if(v.phase==host::Phase::absent) return v.count==0 && !v.key.valid();
    if(!v.key.valid() || !v.durationMs || !v.scoreLimit) return false;
    for(std::size_t i=0;i<v.count;++i) {
        const auto& r=v.rows[i];
        if(!r.account || !r.player || r.side>=(v.mode==match_rules::Mode::clash?2:12)
            || r.name.back()!=0) return false;
        for(std::size_t j=0;j<i;++j) if(r.account==v.rows[j].account || r.player==v.rows[j].player) return false;
    }
    return true;
}
inline bool encode(const View& v,std::span<std::byte> out,std::size_t& written) noexcept {
    written=0;const auto size=kHeader+v.count*kRow;
    if(!valid(v) || out.size()<size) return false;
    std::size_t p{};const auto put=[&](std::uint64_t n,unsigned bytes) {
        for(unsigned i=0;i<bytes;++i) out[p++]=std::byte((n>>(i*8))&255);
    };
    put(0x31564D44,4);put(static_cast<unsigned>(v.phase),1);put(static_cast<unsigned>(v.mode),1);
    put(v.count,1);put(v.winner,1);
    for(auto n:{v.key.owner,v.key.session,v.key.generation,v.elapsedMs,v.remainingMs,v.durationMs,v.endedAt}) put(n,8);
    put(v.scoreLimit,4);put(v.revision,4);for(auto n:v.scores) put(n,4);
    for(std::size_t i=0;i<v.count;++i) { const auto& r=v.rows[i];
        put(r.account,8);put(r.player,8);put(r.kills,4);put(r.deaths,4);put(r.score,4);put(r.side,1);
        for(auto c:r.name) put(static_cast<unsigned char>(c),1);
    }
    written=p;return true;
}
inline bool decode(std::span<const std::byte> in,View& out) noexcept {
    if(in.size()<kHeader) return false;
    std::size_t p{};const auto get=[&](unsigned bytes) {
        std::uint64_t n{};for(unsigned i=0;i<bytes;++i) n|=std::uint64_t(std::to_integer<unsigned char>(in[p++]))<<(i*8);return n;
    };
    if(get(4)!=0x31564D44) return false;
    View v{};v.phase=static_cast<state::activity::crucible::host::Phase>(get(1));
    v.mode=static_cast<state::activity::crucible::match_rules::Mode>(get(1));v.count=static_cast<std::uint8_t>(get(1));
    v.winner=static_cast<std::uint8_t>(get(1));
    if(v.count>12 || in.size()!=kHeader+v.count*kRow) return false;
    v.key.owner=get(8);v.key.session=get(8);v.key.generation=get(8);v.elapsedMs=get(8);v.remainingMs=get(8);v.durationMs=get(8);v.endedAt=get(8);
    v.scoreLimit=static_cast<std::uint32_t>(get(4));v.revision=static_cast<std::uint32_t>(get(4));
    for(auto& n:v.scores) n=static_cast<std::uint32_t>(get(4));
    for(std::size_t i=0;i<v.count;++i) { auto& r=v.rows[i];
        r.account=get(8);r.player=get(8);r.kills=static_cast<std::uint32_t>(get(4));r.deaths=static_cast<std::uint32_t>(get(4));
        r.score=static_cast<std::uint32_t>(get(4));r.side=static_cast<std::uint8_t>(get(1));for(auto& c:r.name) c=static_cast<char>(get(1));
    }
    if(!valid(v)) return false;out=v;return true;
}
}
