#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawn::middleware::bap::activity_message::scene_sense {
struct Output final {
    std::array<std::uint32_t, 32> events{};
    std::uint32_t generationWire{};
    std::uint32_t sourceRevision{};
    std::uint32_t revision{};
    std::uint8_t eventCount{};
    std::uint8_t state{};
    bool delta{};
    bool completed{};
    bool hasSourceRevision{};
};

template<class Reader>
[[nodiscard]] bool read(Reader& reader, Output& output, std::size_t& width) noexcept {
    const auto before = reader.remaining_bits();
    Output result{};
    std::uint64_t value{};
    if (!reader.read(1, value)) return false;
    result.delta = value != 0;
    if (result.delta) {
        if (!reader.read(32, value)) return false;
        result.generationWire = static_cast<std::uint32_t>(value);
        if (!reader.read(1, value)) return false;
        result.completed = value != 0;
        if (!reader.read(1, value)) return false;
        result.hasSourceRevision = value != 0;
        if (result.hasSourceRevision) {
            if (!reader.read(31, value)) return false;
            result.sourceRevision = static_cast<std::uint32_t>(value);
        }
        if (!reader.read(2, value)) return false;
        result.state = static_cast<std::uint8_t>(value);
        if (!reader.read(6, value) || value > result.events.size()) return false;
        result.eventCount = static_cast<std::uint8_t>(value);
        for (std::size_t index = 0; index < result.eventCount; ++index) {
            if (!reader.read(32, value)) return false;
            result.events[index] = static_cast<std::uint32_t>(value);
        }
    }
    // +4D8490 writes raw revision32; +4D81C0 adds one group-owned zero only after the final
    // object, which is not part of this Scene body
    if (!reader.read(32, value)) return false;
    result.revision = static_cast<std::uint32_t>(value);
    width = before - reader.remaining_bits();
    output = result;
    return true;
}
} // namespace dawn::middleware::bap::activity_message::scene_sense
