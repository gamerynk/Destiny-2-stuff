#pragma once
#include <cstdint>
#include <span>
#include "objective_service.h"
#include "../../../middleware/bap/activity_message/native/adventure_dialogue_authority.h"
namespace dawn::state::activity::coo::native_presentation {
inline constexpr std::size_t kDialogueBits=19767, kDirectiveBits=4802;
template<class Writer> bool absent(Writer& w) noexcept {
    return w.write(0x811C9DC5U,32) && w.write(0,7) && w.write(32767U,16);
}
namespace dialogue_wire=middleware::bap::activity_message::native::dialogue;
[[nodiscard]] inline dialogue_wire::Rows dialogue_rows(std::span<const std::uint32_t> generations,std::uint8_t active) noexcept {
    return {generations,active==0xFF?dialogue_wire::kNoRow:active,true};
}
[[nodiscard]] inline std::size_t dialogue_bits(std::span<const std::uint32_t> generations,std::uint8_t active) noexcept {
    if(active!=0xFF && active>=generations.size())return 0;
    for(const auto generation:generations)if(generation>0x7FFFFFFFU)return 0;
    return dialogue_wire::body_bits(dialogue_rows(generations,active));
}
template<class Writer> bool dialogue(Writer& w,std::span<const std::uint32_t> generations,std::uint8_t active) noexcept {
    return dialogue_bits(generations,active)!=0 && dialogue_wire::write(w,dialogue_rows(generations,active));
}

template<class Writer> bool directive_record(Writer& w,bool active,std::uint32_t event,MarkerTarget marker={},bool authored=false,bool nativeMarker=false) noexcept {
    if(!w.write(active?event:0x811C9DC5U,32) || !w.write(0x80000000ULL,32)
        || !w.write(active?1U:0U,2) || !w.write(0,1)) { return false; }
    for(unsigned i=0;i<5;++i) { if(!w.write(authored && i<4?0U:UINT64_MAX,64)) { return false; } }
    if(!w.write(0,32)) { return false; }
    for(unsigned i=0;i<4;++i) { if(!w.write(authored?0x80000000ULL:0x7FFFFFFFULL,32)) { return false; } }
    if(!w.write(1,2) || !absent(w) || !w.write(nativeMarker && active && marker.valid()?3U:1U,3)) { return false; }
    for(unsigned i=0;i<4;++i) {
        const bool selected=active && i==0 && marker.valid();
        if(selected) {
            if(!w.write(marker.asset.registry,32) || !w.write(marker.asset.type+1U,7) || !w.write(marker.asset.slot+32768U,16)) { return false; }
        } else if(!absent(w)) { return false; }
        if(!absent(w)) { return false; }
        for(unsigned j=0;j<4;++j) { if(!w.write(selected?marker.locator[j]:authored?0x811C9DC5U:0U,32)) { return false; } }
        if(!w.write(0,1)) { return false; }
    }
    return true;
}
template<class Writer> bool directive(Writer& w,std::uint32_t event) noexcept {
    if(event==0 || event==UINT32_MAX) { return false; }
    const auto start=w.bit_count();
    return absent(w) && absent(w) && directive_record(w,true,event) && directive_record(w,false,event)
        && directive_record(w,false,event) && w.write(1,3) && w.bit_count()-start==kDirectiveBits;
}
}

namespace dawn::state::activity::coo::native_presentation {
template<class Writer> bool objective(Writer& w,const ObjectiveState& state,Asset audience={},bool authored=false,bool nativeDelivery=false) noexcept {
    if(!state.published) { return false; }
    const auto begin=w.bit_count();
    const bool addressed=audience.registry!=0
        ? w.write(audience.registry,32) && w.write(audience.type+1U,7) && w.write(audience.slot+32768U,16)
        : absent(w);
    if(!addressed || !absent(w))return false;
    const auto selected=nativeDelivery && state.revision?(state.revision-1U)%3U:0U;
    for(unsigned i=0;i<3;++i) {
        const bool current=i==selected;
        if(!directive_record(w,current && state.active,current?state.event:0,current?state.marker:MarkerTarget{},authored,nativeDelivery))return false;
    }
    return w.write(selected+1U,3) && w.bit_count()-begin==kDirectiveBits;
}
}
