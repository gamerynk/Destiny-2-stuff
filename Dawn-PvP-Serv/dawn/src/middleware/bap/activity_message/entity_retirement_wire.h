#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_reader.h"
#include "../../encoding/bit_writer.h"

namespace dawn::middleware::bap::activity_message::entity_retirement {

// decoded scalar words, not the wire-order byte mask used by lease APIs
using Mask = std::array<std::uint32_t, 256>;
inline constexpr std::size_t kAbandonBytes = 1026;
inline constexpr std::size_t kPurgeBytes = 1026;

struct Abandon {
    Mask mask{};
    std::uint8_t selector{};
    std::int8_t reason{};
};

inline bool empty(const Mask& mask) noexcept {
    for (auto word : mask) if (word != 0) return false;
    return true;
}

inline bool subset(const Mask& mask, const Mask& allowed) noexcept {
    for (std::size_t i=0; i<mask.size(); ++i)
        if ((mask[i] & ~allowed[i]) != 0) return false;
    return true;
}

inline Mask from_wire_mask(std::span<const std::byte, 1024> bytes) noexcept {
    Mask result{};
    for (std::size_t i=0; i<result.size(); ++i)
        for (std::size_t j=0; j<4; ++j)
            result[i]=(result[i]<<8)|std::to_integer<std::uint8_t>(bytes[i*4+j]);
    return result;
}

inline bool parse_abandon(std::span<const std::byte> body, Abandon& output) noexcept {
    output={};
    if (body.size()!=kAbandonBytes) return false;
    encoding::bits::Reader reader(body);
    std::uint64_t value{};
    Abandon parsed{};
    if (!reader.read(8,value)) return false;
    parsed.selector=static_cast<std::uint8_t>(value);
    for (auto& word:parsed.mask) {
        if (!reader.read(32,value)) return false;
        word=static_cast<std::uint32_t>(value);
    }
    if (!reader.read(3,value)) return false;
    parsed.reason=static_cast<std::int8_t>(static_cast<int>(value)-1);
    // fixed reflected body has five zero padding bits; refuse trailing or ambiguous input
    if (!reader.read(5,value) || value!=0) return false;
    output=parsed;
    return true;
}

inline bool encode_purge(const Mask& mask, std::int8_t reason, std::uint8_t epoch,
                         std::span<std::byte> output, std::size_t& written) noexcept {
    written=0;
    if (output.size()<kPurgeBytes || reason < -1 || reason > 6) return false;
    encoding::bits::Writer writer(output.first(kPurgeBytes));
    if (!writer.write(static_cast<unsigned>(reason+1),3) || !writer.write(epoch,8)) return false;
    for (auto word:mask) if (!writer.write(word,32)) return false;
    return writer.finish(written) && written==kPurgeBytes;
}

} // namespace dawn::middleware::bap::activity_message::entity_retirement
