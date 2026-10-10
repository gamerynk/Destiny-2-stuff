#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap::activity_message::incident {

inline constexpr std::uint32_t kMessageType = 19;

inline constexpr std::uint8_t kTargetWidth = 13;
// above this the client indexes handler tables unbounded
inline constexpr std::uint32_t kTargetMaximum = 7'762;
// these rows carry type code -1 and crash the client, so they never pass
inline constexpr std::array<std::uint32_t, 3> kPoisonTargets{795, 4'690, 5'375};

inline constexpr std::uint8_t kExtraCountWidth = 5;
inline constexpr std::uint32_t kExtraTargetMaximum = 25;
inline constexpr std::uint8_t kSelectorPresenceWidth = 1;
inline constexpr std::uint8_t kSelectorLengthWidth = 9;
inline constexpr std::uint32_t kSelectorMaximum = 260;
inline constexpr std::uint8_t kOptionalPresenceWidth = 1;
inline constexpr std::uint8_t kOptionalFieldWidth = 64;
inline constexpr std::uint8_t kPayloadLengthWidth = 9;
inline constexpr std::uint32_t kPayloadMaximum = 500;
inline constexpr std::size_t kMinimumBodyBits = kTargetWidth + kExtraCountWidth
                                                + kSelectorPresenceWidth + kOptionalPresenceWidth
                                                + kPayloadLengthWidth;

enum class Verdict : std::uint8_t {
    accepted,
    truncated,
    targetOutOfRange,
    targetPoisoned,
    tooManyTargets,
    payloadTooLong,
    selectorTooLong,
};

struct Incident {
    std::uint32_t primaryTarget{};
    std::uint32_t extraTargets[kExtraTargetMaximum]{};
    std::uint32_t extraTargetCount{};
    std::uint32_t payloadLength{};
    std::array<std::byte, kPayloadMaximum> payload{};
    bool hasCompressedSelector{};
    bool hasPayload{};
};

[[nodiscard]] const char* verdict_name(Verdict verdict) noexcept;

[[nodiscard]] Verdict validate(std::span<const std::byte> payload, Incident& parsed) noexcept;

} // namespace dawn::middleware::bap::activity_message::incident
