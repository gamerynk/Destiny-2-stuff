#pragma once

#include <cstddef>
#include <span>

#include "registry.h"

namespace dawn::client::patterns::game {

enum class Id : std::size_t {
    contentManifestGate,
    bubbleAuthorityDecoder,
    contentUntrackedGetter,
    contentIdTokenLoad,
    queuezObjectResolver,
    queuezFamily5Subscribe,
    getItemStatValue,
    lightValueToScalar,
    retailLogEnqueue,
    retailLogSetCategoryVerbosity,
    count,
};

[[nodiscard]] std::span<const patterns::Pattern> definitions() noexcept;

} // namespace dawn::client::patterns::game
