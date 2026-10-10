#pragma once
#include <cstdint>
#include <span>
#include "../../../middleware/encoding/bit_reader.h"
namespace dawn::state::activity::coo::native_player_trigger {
inline constexpr std::uint32_t kIncident=6685;
inline constexpr std::size_t kAuthBits=129,kPayloadBytes=53;
struct Receipt final { std::uint32_t registry{},object{};std::int16_t slot{-1};std::int8_t type{-1}; };
[[nodiscard]] inline bool decode(std::span<const std::byte> payload,Receipt& result) noexcept {
    result={};if(payload.size()!=kPayloadBytes) { return false; }
    middleware::encoding::bits::Reader reader(payload);
    std::uint64_t registry{},type{},slot{},object{},padding{};
    if(!reader.skip(335) || !reader.read(32,registry) || !reader.read(7,type)
        || !reader.read(16,slot) || !reader.read(32,object) || !reader.read(2,padding)
        || padding!=0 || reader.remaining_bits()!=0) { return false; }
    result={static_cast<std::uint32_t>(registry),static_cast<std::uint32_t>(object),
        static_cast<std::int16_t>(static_cast<int>(slot)-32768),
        static_cast<std::int8_t>(static_cast<int>(type)-1)};
    return result.registry!=0 && result.registry!=0x811C9DC5U && result.type==31 && result.slot>=0;
}
template<class Writer> [[nodiscard]] bool arm(Writer& writer,std::uint64_t generation) noexcept {
    return generation!=0 && writer.write(1,1) && writer.write(generation,64) && writer.write(0,64);
}
}
