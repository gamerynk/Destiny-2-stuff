#pragma once
#include <cstdint>

namespace dawn::middleware::gameplay::peer {
// conservative server policy: a low-seven-bit ACK alone doesn't identify a full outgoing
// sequence, the ACK-base ledger must qualify it too
struct InboundAckContext {
    std::uint16_t newestHead = 0;
    bool initialized = false;
    std::uint16_t confirmedBase = 0;
    std::uint64_t confirmedOrdinal = 0;
};

[[nodiscard]] inline bool accept_ack_context(InboundAckContext& context,
                                              bool headPresent,
                                              std::uint16_t head) noexcept {
    constexpr std::uint16_t modulus = 1024;
    if (!headPresent || head >= modulus) return false;
    if (context.initialized) {
        const auto advance = (head + modulus - context.newestHead) % modulus;
        if (!advance || advance >= modulus / 2) return false;
    }
    context.newestHead = head;
    context.initialized = true;
    return true;
}

// resolve only from the retained ACK anchor, never relative to the latest outgoing packet
// after a ring wrap; first issuance is 1, zero is the initial anchor, jumps of 64+ fail closed
[[nodiscard]] inline bool resolve_ack_context(InboundAckContext& context,
                                               bool headPresent,
                                               std::uint16_t head,
                                               std::uint16_t rawBase,
                                               std::uint64_t latestIssuedOrdinal,
                                               std::uint16_t& resolvedBase) noexcept {
    if (rawBase >= 128 || context.confirmedBase >= 1024
        || context.confirmedBase != context.confirmedOrdinal % 1024
        || latestIssuedOrdinal < context.confirmedOrdinal)
        return false;
    auto candidateContext = context;
    if (!accept_ack_context(candidateContext, headPresent, head)) return false;
    const auto advance = (rawBase + 128 - context.confirmedBase % 128) % 128;
    if (advance >= 64) return false;
    const auto candidate = static_cast<std::uint16_t>((context.confirmedBase + advance) % 1024);
    // issuance distance never becomes young again after a modulo wrap; the caller must pass a
    // per-connection monotonically increasing ordinal
    const auto issuedAdvance = latestIssuedOrdinal - context.confirmedOrdinal;
    if (issuedAdvance >= 512 || advance > issuedAdvance) return false;
    candidateContext.confirmedBase = candidate;
    candidateContext.confirmedOrdinal += advance;
    context = candidateContext;
    resolvedBase = candidate;
    return true;
}
}
