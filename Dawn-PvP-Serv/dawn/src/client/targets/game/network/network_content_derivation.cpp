#include "network_content_derivation.h"
#include "../relative.h"

namespace dawn::client::targets::game::network::content_derivation {

bool derive(std::span<const patterns::ImageRange> image,
            std::byte* gate,
            Targets& resolved) noexcept {
    // the native GET, fetch and content tick stay untouched; only the writable gate is owned
    constexpr std::size_t displacementOffset = 15;
    constexpr std::size_t instructionEnd = 20;
    return relative::contains(image, gate, instructionEnd)
        && relative::resolve(gate, displacementOffset, instructionEnd,
                             resolved.contentManifestSignatureGate);
}

} // namespace dawn::client::targets::game::network::content_derivation
