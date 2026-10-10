#pragma once
#include "native_presentation_authority.h"
namespace dawn::state::activity::coo::native_music {
template<class Writer> bool select(Writer& w,std::uint8_t candidate) noexcept {
    if(candidate>=128) { return false; }
    for(unsigned word=0;word<4;++word) {
        if(!w.write(static_cast<unsigned>(candidate)/32U==word?std::uint32_t{1}<<(candidate%32):0U,32)) { return false; }
    }
    for(unsigned i=0;i<129;++i) { if(!native_presentation::absent(w)) { return false; } }
    return true;
}
}
