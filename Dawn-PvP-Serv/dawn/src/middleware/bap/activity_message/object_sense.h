#pragma once
#include <bit>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::object_sense {
// 8080992E is a full object snapshot; its polymorphic reply list holds the native
// accepted-use latch (80804FB7) and optional carry ownership (80809ACC)
struct Output {
    std::int32_t generation{},entry{},useRevision{};
    std::uint64_t owner{};
    bool alive{},present{},hasUse{},used{},hasOwner{},held{};
};
template<class Reader> bool read(Reader& r,Output& out) noexcept {
    Output v{};std::uint64_t x{},count{};bool device{};
    const auto integer=[&](std::int32_t& value) {
        if(!r.read(32,x)) {return false;}
        value=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(x)^0x80000000U);return true;
    };
    const auto flag=[&](bool& value) {if(!r.read(1,x)) {return false;}value=x!=0;return true;};
    if(!integer(v.generation) || !flag(v.alive) || !flag(v.present)
        || !integer(v.entry) || !r.skip(64) || !r.read(2,count)) {return false;}
    for(std::uint64_t i=0;i<count;++i) {
        if(!r.read(1,x)) {return false;}if(!x) {continue;}
        if(!r.read(32,x)) {return false;}
        if(x==0x80804FB7U) {
            if(v.hasUse || !flag(v.used) || !integer(v.useRevision)) {return false;}v.hasUse=true;
        } else if(x==0x80809ACCU) {
            if(v.hasOwner || !flag(v.held) || !r.read(64,v.owner)) {return false;}v.hasOwner=true;
        } else if(x==0x80805062U) {
            // native device reply: three revision/snap-revision/float triples; light completion
            // uses the separately owned native observation
            if(device || !r.skip(9U*32U)) {return false;}device=true;
        } else {return false;}
    }
    out=v;return true;
}
}
