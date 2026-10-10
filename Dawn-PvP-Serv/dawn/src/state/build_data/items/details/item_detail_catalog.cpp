#include "item_detail_catalog.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <limits>
#include <span>
#include <vector>

#include "../../table.h"
#include "../item_catalog.h"

namespace dawn::state::build_data::items::details {
namespace {

constexpr std::size_t kNativeDefinitionIndexCapacity =
    static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)()) + 1U;
constexpr std::uint16_t kEmptyLookupRow = (std::numeric_limits<std::uint16_t>::max)();

Lock g_lock;
std::vector<Definition> g_definitions;
std::size_t g_definitionCount{};
std::array<std::uint16_t, kNativeDefinitionIndexCapacity> g_lookup{};

static_assert(kDefinitionCapacity < kEmptyLookupRow);

[[nodiscard]] bool equipment_slot_valid(const Definition& definition) noexcept {
    if (!definition.equipmentSlot.has_value()) {
        return true;
    }
    const std::int8_t slot = *definition.equipmentSlot;
    return slot >= 0 && static_cast<std::size_t>(slot) < kEquipmentSlotCount;
}

[[nodiscard]] bool instance_state_valid(InstancedDefinitionState state) noexcept {
    return state == InstancedDefinitionState::stackable
           || state == InstancedDefinitionState::instanced;
}

[[nodiscard]] bool ordinary_sockets_valid(const Definition& definition) noexcept {
    if (definition.ordinarySocketCount > kInitialPlugCapacity) {
        return false;
    }

    std::size_t tailStart = 0;
    switch (definition.ordinarySocketState) {
    case OrdinarySocketState::absent:
        if (definition.ordinarySocketCount != 0) {
            return false;
        }
        break;
    case OrdinarySocketState::present:
        tailStart = definition.ordinarySocketCount;
        break;
    default:
        return false;
    }

    const auto unusedTail = std::span(definition.initialPlugIndices).subspan(tailStart);
    return std::all_of(unusedTail.begin(), unusedTail.end(), [](std::uint16_t index) {
        return index == kUnavailableItemIndex;
    });
}

[[nodiscard]] bool definition_valid(const Definition& definition) noexcept {
    return definition.bucketId != items::kUnresolvedBucketId && definition.maxStackSize > 0
           && instance_state_valid(definition.instancedDefinitionState)
           && equipment_slot_valid(definition) && ordinary_sockets_valid(definition);
}

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_definitions.clear();
    g_definitions.shrink_to_fit();
    g_definitionCount = 0;
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
}

bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }

    std::bitset<kNativeDefinitionIndexCapacity> occupied{};
    for (const Definition& definition : definitions) {
        if (occupied.test(definition.definitionIndex) || !definition_valid(definition)) {
            return false;
        }
        occupied.set(definition.definitionIndex);
    }
    return true;
}

bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }

    std::vector<Definition> staged(definitions.begin(), definitions.end());

    const Lock::Exclusive guard(g_lock);
    std::fill(g_lookup.begin(), g_lookup.end(), kEmptyLookupRow);
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        g_lookup[definitions[index].definitionIndex] = static_cast<std::uint16_t>(index);
    }
    g_definitions = std::move(staged);
    g_definitionCount = definitions.size();
    return true;
}

bool find(std::uint16_t definitionIndex, Definition& definition) noexcept {
    definition = {};
    const Lock::Shared guard(g_lock);
    const std::span<const Definition> rows{g_definitions.data(), g_definitionCount};
    const std::uint16_t row = g_lookup[definitionIndex];
    const bool found = row != kEmptyLookupRow && row < rows.size();
    if (found) {
        definition = rows[row];
    }
    return found;
}

bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    count = 0;
    if (output.size() < g_definitionCount) {
        return false;
    }
    if (g_definitionCount != 0) {
        std::copy_n(g_definitions.data(), g_definitionCount, output.begin());
    }
    count = g_definitionCount;
    return true;
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitionCount;
}

} // namespace dawn::state::build_data::items::details
