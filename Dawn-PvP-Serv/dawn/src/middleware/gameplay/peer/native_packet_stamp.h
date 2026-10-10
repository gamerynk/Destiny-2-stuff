#pragma once
#include <bit>
#include <cstdint>
namespace dawn::middleware::gameplay::peer {
struct NativeStampContext {std::int32_t priorHead{};bool proven{};};
struct NativeStampResult {std::int32_t stamp{},unwrapped{},nextHead{};};
// matches 17D622E..17D626A: asymmetric native -895..128 unwrap. Caller must supply the actual
// native prior head, low10 and cursor7; output is staged until full acceptance
[[nodiscard]] inline bool native_packet_stamp(const NativeStampContext& context,
    std::uint16_t low10,bool present,std::uint8_t cursor7,NativeStampResult& output) noexcept {
    if(!context.proven||low10>=1024||cursor7>=128)return false;
    auto delta=static_cast<std::int32_t>(low10)-static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(context.priorHead)&1023);
    if(delta>128)delta-=1024;else if(delta<=-896)delta+=1024;
    const auto unwrapped=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(context.priorHead)+static_cast<std::uint32_t>(delta));
    const auto candidate=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(unwrapped)-cursor7);
    output={present?unwrapped:-1,unwrapped,candidate>=context.priorHead?candidate:context.priorHead};
    return true;
}
}
