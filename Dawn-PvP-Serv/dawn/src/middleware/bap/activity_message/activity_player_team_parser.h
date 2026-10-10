#pragma once
#include "../../../core/protocol/activity_player_team.h"
#include "../../encoding/bit_reader.h"

namespace dawn::middleware::bap::activity_message::player_team {
inline bool decode(std::span<const std::byte> blob,
                   core::protocol::ActivityPlayerTeam& output) noexcept {
    output={};
    encoding::bits::Reader reader(blob);
    const auto presence=[&](bool& value) noexcept {
        std::uint64_t wire{};
        if (!reader.read(1,wire)) return false;
        value=wire!=0;return true;
    };
    const auto optional=[&](std::size_t width) noexcept {
        bool present{};
        return presence(present) && (!present || reader.skip(width));
    };
    const auto name=[&]() noexcept {
        bool present{};
        if (!presence(present)) return false;
        if (!present) return true;
        for (unsigned i=0;i<64;++i) {
            std::uint64_t character{};
            if (!reader.read(16,character)) return false;
            if (character==0) return true;
        }
        return false;
    };
    std::uint64_t incarnation{},header{},wire{};
    if (!reader.read(3,incarnation) || incarnation<1 || incarnation>4
        || !reader.read(1,header) || header!=0) return false;
    core::protocol::ActivityPlayerTeam parsed{};
    if (!name() || !presence(parsed.hasAssignment)
        || (parsed.hasAssignment && !reader.read(6,wire))) return false;
    if (parsed.hasAssignment) parsed.assignment=static_cast<std::int8_t>(wire)-1;
    if (!optional(6) || !optional(2+6) || !name()
        // native A49770 writes 288 bits, not the in-memory 48-byte identity struct
        || !optional(36*8) || !optional(8) || !presence(parsed.hasRequestedTeam)
        || (parsed.hasRequestedTeam && !reader.read(6,wire))) return false;
    if (parsed.hasRequestedTeam) parsed.requestedTeam=static_cast<std::int8_t>(wire)-1;
    if (!optional(6) || !optional(2*16) || !optional(2*64)
        || !optional(32+4*16+8) || !optional(32)) return false;
    output=parsed;
    return true;
}
}
