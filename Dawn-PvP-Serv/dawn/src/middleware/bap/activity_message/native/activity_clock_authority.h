#pragma once
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include "../../../encoding/bit_writer.h"

namespace dawn::middleware::bap::activity_message::native::activity_clock {
// service-9 notification 2: 8080867E wraps 808086E8. Event 26 -> 3CB7A0 stores f0 at
// scenario+14, f1 at +18. f1 is timing config (4E4CA0: 1000/f1), not a rate multiplier
inline constexpr std::uint32_t kMessageType=2;
inline constexpr std::uint32_t kSchema=0x8080867E;
inline constexpr std::size_t kBits=33;
struct Configuration final {
    bool field0{};
    float timing{};
};
// our own timing policy: keep the native 4E4CA0 fallback's 30 ticks/s but enable 3CB030
// interpolation between packets. Not a recovered server setting, unrelated to match length
inline constexpr Configuration kRunningCrucible{false, 1000.0F / 30.0F};
[[nodiscard]] inline bool valid(Configuration value) noexcept {
    return std::isfinite(value.timing) && value.timing>=0.0F;
}
template<class Writer> [[nodiscard]] bool write(Writer& writer,Configuration value) noexcept {
    return valid(value) && writer.write(value.field0?1:0,1)
        && writer.write(std::bit_cast<std::uint32_t>(value.timing),32);
}
[[nodiscard]] inline bool encode(Configuration value, std::span<std::byte> output,
    std::size_t& written) noexcept {
    written = 0;
    if (!valid(value) || output.size() < (kBits + 7) / 8) return false;
    encoding::bits::Writer writer(output);
    return write(writer, value) && writer.finish(written);
}
// type5 uses 351070(reader,&native_u64,64), a raw memory-bit copy, not a reflected numeric
// scalar, so its eight bytes must be little endian even though each is written MSB first
template<class Writer> [[nodiscard]] bool write_elapsed(Writer& writer,std::uint64_t ticks) noexcept {
    for(unsigned shift=0;shift<64;shift+=8)
        if(!writer.write((ticks>>shift)&0xFF,8))return false;
    return true;
}
inline constexpr std::uint64_t kTicksPerSecond=673200;
[[nodiscard]] constexpr bool from_milliseconds(std::uint64_t milliseconds,
    std::uint64_t& ticks) noexcept {
    const auto whole=milliseconds/1000;
    const auto fraction=(milliseconds%1000)*kTicksPerSecond/1000;
    if(whole>(UINT64_MAX-fraction)/kTicksPerSecond)return false;
    ticks=whole*kTicksPerSecond+fraction;return true;
}
}
