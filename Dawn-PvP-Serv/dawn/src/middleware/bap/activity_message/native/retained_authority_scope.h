#pragma once
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::native::authority_scope {
// a retained roster describes an owner's existing native objects; selecting another region
// must not replace their authority with schema defaults. Validates scope only
template<class Roster>
[[nodiscard]] bool valid(const Roster& roster,std::uint32_t key,
    std::uint32_t bubble,std::uint32_t region) noexcept {
    if(bubble>63 || region>504 || region%8U!=0 || roster.bubbleSubBlocks.size()>64) return false;
    unsigned matches{};
    bool retained{};
    for(const auto& block:roster.bubbleSubBlocks) {
        if(block.bubble>63 || block.keys.size()>96
            || (!block.presence.empty() && block.presence.size()!=block.keys.size())) return false;
        for(const auto present:block.presence) if(present>1) return false;
        bool fullStates{};
        if constexpr(requires { block.states; }) {
            if(!block.states.empty() && block.states.size()!=block.keys.size()) return false;
            for(const auto state:block.states) if(state<0x80U) return false;
            fullStates=!block.states.empty();
        }
        for(std::size_t k=0;k<block.keys.size();++k) if(block.keys[k]==key) {
            if(block.bubble!=bubble || (!block.presence.empty() && block.presence[k]!=1)) return false;
            ++matches;
            retained=fullStates && block.presence.size()==block.keys.size()
                && block.presence[k]==1;
        }
    }
    return matches==1 && (region==bubble*8U || retained);
}
}
