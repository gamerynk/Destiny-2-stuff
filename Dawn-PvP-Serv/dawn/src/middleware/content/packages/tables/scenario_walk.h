#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "scenario_reader.h"

namespace dawn::middleware::content::packages::tables {

// each slot needs its own storage to stay live
enum class ReadSlot : std::uint8_t {
    entry,
    registry,
    object,
    count,
};

using TagReader = bool (*)(void* context,
                           ReadSlot slot,
                           std::uint32_t tag,
                           std::span<const std::byte>& blob) noexcept;

struct Placement {
    std::uint32_t bubbleHash{};
    std::uint32_t stateHash{};
    std::uint32_t entryTag{};
    std::uint32_t sliceSetIndex{};
    std::uint32_t registryTag{};
    std::uint32_t objectTag{};
    std::uint32_t objectKey{};
    std::uint64_t slotCount{};
    // valid only during the visitor call; the next object read replaces it
    std::span<const std::byte> objectBytes{};
    std::uint64_t bubbleIndex{};
    std::uint64_t stateIndex{};
    std::size_t registryDescriptor{};
    std::uint64_t objectIndex{};
    bool stateEnabled{};
    bool bubblePublic{};
};

struct WalkResult {
    std::uint64_t bubbles{};
    std::uint64_t states{};
    std::uint64_t entries{};
    std::uint64_t registries{};
    std::uint64_t placements{};
    std::uint64_t unresolved{};
};

using PlacementVisitor = bool (*)(void* context, const Placement& placement) noexcept;

[[nodiscard]] bool walk_scenario(std::span<const std::byte> blob,
                                 TagReader reader,
                                 void* readerContext,
                                 PlacementVisitor visitor,
                                 void* visitorContext,
                                 WalkResult& output) noexcept;

} // namespace dawn::middleware::content::packages::tables
