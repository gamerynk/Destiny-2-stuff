#include "../runtime.h"
#include "../runtime/persistence/publication_transaction.h"
#include "collectible_catalog.h"

namespace dawn::state::build_data {
namespace {

[[nodiscard]] bool
valid_publication(std::span<const collectibles::Definition> definitions) noexcept {
    if (!item_definitions_ready() || !collectibles::valid(definitions)) {
        return false;
    }
    const std::size_t itemCount = items::count();
    for (const collectibles::Definition& definition : definitions) {
        items::Definition item{};
        if (definition.itemDefinitionIndex != collectibles::kUnavailableItemDefinitionIndex
            && (definition.itemDefinitionIndex >= itemCount
                || !items::find_index(definition.itemDefinitionIndex, item)
                || item.definitionIndex != definition.itemDefinitionIndex)) {
            return false;
        }
        for (std::size_t index = 0; index < definition.materialRequirementCount; ++index) {
            const collectibles::MaterialRequirement& requirement =
                definition.materialRequirements[index];
            if (requirement.itemDefinitionIndex >= itemCount
                || !items::find_index(requirement.itemDefinitionIndex, item)
                || item.definitionIndex != requirement.itemDefinitionIndex) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool collectible_definitions_ready() noexcept {
    return collectibles::count() != 0;
}

bool publish_collectible_definitions(
    std::span<const collectibles::Definition> definitions) noexcept {
    runtime::persistence::Transaction transaction;
    return transaction.active() && valid_publication(definitions)
           && transaction.finish(collectibles::replace(definitions), collectibles::clear);
}

bool find_collectible_item_definition_index(std::uint16_t collectibleIndex,
                                            std::uint16_t& itemDefinitionIndex) noexcept {
    itemDefinitionIndex = collectibles::kUnavailableItemDefinitionIndex;
    collectibles::Definition definition{};
    if (!collectible_definitions_ready() || !collectibles::find(collectibleIndex, definition)
        || definition.collectibleIndex != collectibleIndex
        || definition.itemDefinitionIndex == collectibles::kUnavailableItemDefinitionIndex) {
        return false;
    }
    itemDefinitionIndex = definition.itemDefinitionIndex;
    return true;
}

bool find_collectible_definition(std::uint16_t collectibleIndex,
                                 collectibles::Definition& definition) noexcept {
    definition = {};
    return collectible_definitions_ready() && collectibles::find(collectibleIndex, definition)
           && definition.collectibleIndex == collectibleIndex;
}

} // namespace dawn::state::build_data
