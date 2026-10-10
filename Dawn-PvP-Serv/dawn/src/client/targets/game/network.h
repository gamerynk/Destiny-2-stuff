#pragma once

#include <cstddef>
#include <span>

#include "../../patterns/registry.h"

namespace dawn::client::targets::game::network {

struct Targets {
    std::byte* contentManifestSignatureGate{};
    std::byte* bubbleAuthorityDecoder{};
    std::byte* contentUntrackedGetter{};
    std::byte* contentIdToken{};
};

[[nodiscard]] bool resolve(std::span<const patterns::ImageRange> image) noexcept;

void clear() noexcept;

[[nodiscard]] const Targets& get() noexcept;

} // namespace dawn::client::targets::game::network
