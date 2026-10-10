#pragma once
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::native::engagement_sense {
inline constexpr std::uint32_t kSchema=0x808094F0;
struct Output final {
    std::uint32_t revision{};
    std::int16_t generation{-1};
    std::uint8_t participantCount{};
    bool root{};
};
template<class Reader>
[[nodiscard]] bool read(Reader& reader,Output& output,std::size_t& width) noexcept {
    output={};width=0;Output result{};
    const auto before=reader.remaining_bits();std::uint64_t value{};
    if(!reader.read(1,value))return false;
    result.root=value!=0;
    if(result.root){
        if(!reader.read(5,value) || value>16)return false;
        result.participantCount=static_cast<std::uint8_t>(value);
        if(!reader.skip(value*64) || !reader.read(16,value))return false;
        result.generation=static_cast<std::int16_t>(static_cast<std::int32_t>(value)-32768);
    }
    if(!reader.read(32,value))return false;
    result.revision=static_cast<std::uint32_t>(value);
    width=before-reader.remaining_bits();output=result;return true;
}
}
