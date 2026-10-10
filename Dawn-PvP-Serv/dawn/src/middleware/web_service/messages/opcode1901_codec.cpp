#include <cstddef>

#include "../../encoding/bit_reader.h"
#include "opcode1901.h"

namespace dawn::middleware::web_service::messages::opcode1901 {
namespace {

constexpr std::uint8_t kReplacementCountWidth = 4;
constexpr std::size_t kBitsPerByte = 8;
constexpr std::uint8_t kDefinitionIndexWidth = 15;
constexpr std::uint8_t kCanonicalSocketKindWidth = 8;
constexpr std::uint8_t kModelSocketKindWidth = 2;
constexpr std::uint8_t kSocketIndexWidth = 32;
constexpr std::uint8_t kOptionalIdentityWidth = 64;
constexpr std::uint64_t kCanonicalSocketKindBias = 0x80ULL;
constexpr std::uint64_t kSocketIndexBias = 0x80000000ULL;
// model socket-kind 0 is encoded as 1 so wire zero stays the absent sentinel
constexpr std::uint64_t kModelSocketKindBias = 1;
constexpr std::uint64_t kShaderModelSocketKind = 0;
constexpr std::uint64_t kShaderAuxiliary = 0;
// the auxiliary identity is 63 bits; the next 64 are the target's whole instance identity
constexpr std::uint8_t kAuxiliaryWidth = 63;
constexpr std::uint8_t kTrailingWidth = 2;

} // namespace

bool identifies_instance(std::uint64_t instanceIdentityToken, std::uint64_t instanceSoid) noexcept {
    return instanceIdentityToken != 0 && instanceSoid == instanceIdentityToken;
}

bool parse_request(const Message& message, Request& request) noexcept {
    request = {};
    if (message.opcode != kOpcode) {
        return false;
    }

    encoding::bits::Reader reader(message.payload);
    std::uint64_t replacementCount = 0;
    if (!reader.read(kReplacementCountWidth, replacementCount) || replacementCount == 0
        || replacementCount > kReplacementCapacity) {
        return false;
    }

    // the run is counted, so its width follows the count; reading that many and requiring the payload
    // to be spent proves the descriptor's length
    for (std::uint64_t index = 0; index < replacementCount; ++index) {
        std::uint64_t plugDefinitionPresent = 0;
        std::uint64_t encodedPlugDefinition = 0;
        std::uint64_t encodedCanonicalSocketKind = 0;
        std::uint64_t modelSocketKind = 0;
        std::uint64_t encodedSocketIndex = 0;
        std::uint64_t auxiliaryPresent = 0;
        std::uint64_t auxiliary = 0;
        if (!reader.read(1, plugDefinitionPresent)
            || !reader.read(kDefinitionIndexWidth, encodedPlugDefinition)
            || !reader.read(kCanonicalSocketKindWidth, encodedCanonicalSocketKind)
            || !reader.read(kModelSocketKindWidth, modelSocketKind)
            || !reader.read(kSocketIndexWidth, encodedSocketIndex)
            || !reader.read(1, auxiliaryPresent) || !reader.read(kAuxiliaryWidth, auxiliary)
            || plugDefinitionPresent == 0 || auxiliaryPresent == 0
            || encodedCanonicalSocketKind < kCanonicalSocketKindBias
            || modelSocketKind < kModelSocketKindBias || encodedSocketIndex < kSocketIndexBias
            || modelSocketKind - kModelSocketKindBias != kShaderModelSocketKind
            || auxiliary != kShaderAuxiliary) {
            request = {};
            return false;
        }
        Replacement& replacement = request.replacements[static_cast<std::size_t>(index)];
        replacement.plugDefinitionIndex = static_cast<std::uint16_t>(encodedPlugDefinition);
        replacement.canonicalSocketKind =
            static_cast<std::uint8_t>(encodedCanonicalSocketKind - kCanonicalSocketKindBias);
        replacement.modelSocketKind =
            static_cast<std::uint8_t>(modelSocketKind - kModelSocketKindBias);
        replacement.socketIndex = static_cast<std::uint32_t>(encodedSocketIndex - kSocketIndexBias);
        replacement.auxiliary = auxiliary;
        if (replacement.canonicalSocketKind != replacement.socketIndex) {
            request = {};
            return false;
        }
    }
    request.replacementCount = static_cast<std::size_t>(replacementCount);

    // the target is the whole instance identity plus two zero bits. captures confirm it: starter item
    // 0x4000000000000004 and minted item 0x9EAA000100200109 (same bytes opcode 403 sent) both read back exactly
    std::uint64_t trailing = 0;
    if (!reader.read(kOptionalIdentityWidth, request.equipmentSelector)
        || !reader.read(kTrailingWidth, trailing) || reader.remaining_bits() >= kBitsPerByte
        || trailing != 0 || request.equipmentSelector == 0) {
        const std::uint64_t selector = request.equipmentSelector;
        request = {};
        request.equipmentSelector = selector;
        return false;
    }
    request.instanceIdentityToken = request.equipmentSelector;
    return true;
}

} // namespace dawn::middleware::web_service::messages::opcode1901
