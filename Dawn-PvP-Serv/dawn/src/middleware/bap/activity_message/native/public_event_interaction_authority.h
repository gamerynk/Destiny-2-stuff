#pragma once
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::native::interaction {
// native 80804FB8 command in the type4 source's 80809AEA override list, dispatched
// 9EF680 -> B31960 -> F33930; enabling invalidates the prompt cache
enum class Mode : std::uint8_t { unchanged, disabled, enabled };
inline constexpr std::size_t kPayloadBits=122;
[[nodiscard]] constexpr bool valid(Mode mode) noexcept {
    return mode==Mode::unchanged || mode==Mode::disabled || mode==Mode::enabled;
}
template<class Writer> [[nodiscard]] bool write_record(Writer& writer,Mode mode) noexcept {
    if(!valid(mode))return false;
    if(mode==Mode::unchanged)return writer.write(0,1);
    // authored rally setup has an absent scoped override and zero command revision; keep that
    // predicate and leave native use counters alone
    return writer.write(1,1) && writer.write(0x80804FB8U,32)
        && writer.write(mode==Mode::enabled?3:2,2)
        && writer.write(0x811C9DC5U,32) && writer.write(0,7) && writer.write(32767,16)
        && writer.write(0x80000000U,32) && writer.write(0,1);
}
}
