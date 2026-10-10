#pragma once
#include "entity_retirement_wire.h"

namespace dawn::middleware::bap::activity_message::entity_retirement {
inline bool encode_query(std::uint32_t correlation,std::span<std::byte> output,
                         std::size_t& written) noexcept {
    written=0;
    // native idle sentinel is decoded -1 (wire 0x7FFFFFFF); use positive signed correlations
    if (output.size()<4 || correlation<=0x80000000U) return false;
    for (unsigned i=0;i<4;++i) output[i]=std::byte((correlation>>(24-i*8))&255);
    written=4;return true;
}
struct QueryReply {std::uint32_t correlation{};unsigned selector{};Mask mask{};};
inline bool parse_query_reply(unsigned type,std::span<const std::byte> body,QueryReply& out) noexcept {
    out={};
    if ((type!=31 && type!=32) || body.size()!=(type==31?1029U:1028U)) return false;
    QueryReply reply{};
    for (unsigned i=0;i<4;++i) reply.correlation=(reply.correlation<<8)|std::to_integer<unsigned>(body[i]);
    if (type==31) {reply.selector=std::to_integer<unsigned>(body[4]);if(reply.selector>64)return false;}
    reply.mask=from_wire_mask(std::span<const std::byte,1024>(body.subspan(type==31?5:4)));
    out=reply;return true;
}
}
