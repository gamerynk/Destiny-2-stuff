#include "collectible_catalog.h"

#include <array>

#include "../table.h"

namespace dawn::state::build_data::collectibles {
namespace {

Lock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_definitions.clear();
}

bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }
    std::array<bool, kDefinitionCapacity> occupied{};
    for (const Definition& definition : definitions) {
        if (definition.collectibleIndex >= definitions.size()
            || occupied[definition.collectibleIndex]
            || definition.materialRequirementCount > definition.materialRequirements.size()) {
            return false;
        }
        const bool hasRequirements = definition.materialRequirementCount != 0;
        if (hasRequirements
                != (definition.materialRequirementSetIndex
                    != kUnavailableMaterialRequirementSetIndex)
            || hasRequirements != (definition.materialRequirementSetHash != 0)) {
            return false;
        }
        for (std::size_t index = 0; index < definition.materialRequirements.size(); ++index) {
            const MaterialRequirement& requirement = definition.materialRequirements[index];
            if (index < definition.materialRequirementCount) {
                if (requirement.itemDefinitionIndex == kUnavailableItemDefinitionIndex
                    || requirement.quantity == 0) {
                    return false;
                }
                for (std::size_t prior = 0; prior < index; ++prior) {
                    if (definition.materialRequirements[prior].itemDefinitionIndex
                        == requirement.itemDefinitionIndex) {
                        return false;
                    }
                }
            } else if (requirement.itemDefinitionIndex != kUnavailableItemDefinitionIndex
                       || requirement.quantity != 0 || requirement.deleteOnAction
                       || requirement.omitFromRequirements) {
                return false;
            }
        }
        occupied[definition.collectibleIndex] = true;
    }
    return true;
}

bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    const Lock::Exclusive guard(g_lock);
    const std::span<Definition> storage = g_definitions.reset(definitions.size());
    if (storage.size() != definitions.size()) {
        return false;
    }
    for (const Definition& definition : definitions) {
        storage[definition.collectibleIndex] = definition;
    }
    return true;
}

bool find(std::uint16_t collectibleIndex, Definition& definition) noexcept {
    definition = {};
    definition.itemDefinitionIndex = kUnavailableItemDefinitionIndex;
    const Lock::Shared guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    const bool found = static_cast<std::size_t>(collectibleIndex) < rows.size();
    if (found) {
        definition = rows[collectibleIndex];
    }
    return found;
}

bool grants_item(std::uint16_t itemDefinitionIndex) noexcept {
    if (itemDefinitionIndex == kUnavailableItemDefinitionIndex) {
        return false;
    }
    const Lock::Shared guard(g_lock);
    for (const Definition& definition : g_definitions.rows()) {
        if (definition.itemDefinitionIndex == itemDefinitionIndex) {
            return true;
        }
    }
    return false;
}

bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.snapshot(output, count);
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.count();
}

} // namespace dawn::state::build_data::collectibles
