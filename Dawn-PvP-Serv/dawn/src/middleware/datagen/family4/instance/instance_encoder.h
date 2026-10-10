#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "layout.h"

namespace dawn::middleware::datagen::family4::instance {

enum class SocketEntryState : std::uint8_t {
    absent = 0,
    // entry exists but has no runtime selection yet
    ready = 16,
    // selected by the character, or it is the super lane
    active = 18,
};

struct SocketSelector {
    std::uint8_t entry{0xFF};
    std::uint8_t group{0xFF};
    std::uint8_t element{0xFF};
};

enum class OrdinarySocketBlockState : std::uint8_t {
    unresolved = 0,
    absent = 1,
    present = 2,
};

struct OrdinarySockets {
    OrdinarySocketBlockState state{OrdinarySocketBlockState::unresolved};
    std::array<std::optional<std::uint16_t>, layout::kOrdinarySocketCapacity> plugs{};
};

struct DefinitionBounds {
    std::uint32_t itemDefinitionCount{};
    std::uint32_t socketEntryListCount{};
};

struct ResolvedInstance {
    std::uint64_t instanceSoid{};
    DefinitionBounds bounds{};
    std::uint16_t baseDefinitionIndex{layout::kEmptyDefinitionIndex};
    std::int32_t level{};
    std::uint16_t curveSelector{layout::kInitialLevelCurveX};
    std::uint16_t capSelector{layout::kInitialLevelCapRow};
    std::uint16_t socketEntryListIndex{layout::kEmptyDefinitionIndex};
    std::uint8_t socketEntryCount{};
    bool socketEntryContentsResolved{};
    OrdinarySockets ordinarySockets{};
    std::array<SocketEntryState, layout::kSocketEntryStateCapacity> socketEntryStates{};
    std::array<SocketSelector, layout::kSelectorCapacity> socketSelectors{};
};

[[nodiscard]] bool encode(const ResolvedInstance& input, std::span<std::byte> output) noexcept;

} // namespace dawn::middleware::datagen::family4::instance
