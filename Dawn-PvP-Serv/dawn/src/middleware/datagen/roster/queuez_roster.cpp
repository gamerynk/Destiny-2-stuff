#include "queuez_roster.h"

#include <limits>

#include "../family4/loadout/loadout_resolver.h"

namespace dawn::middleware::datagen::roster {

bool initialize(const state::AccountState& account, Block& block) noexcept {
    constexpr std::uint16_t kEmptyDefinitionIndex = (std::numeric_limits<std::uint16_t>::max)();
    if (!state::account::valid(account) || account.characterCount > block.characters.size()) {
        return false;
    }
    block = {};
    block.characterCount = static_cast<std::uint32_t>(account.characterCount);
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        block.characters[index].characterSoid = account.characters[index].soid;
    }
    // only an occupied row carries empty markers; an unused row stays all zero, like the
    // reference rosters and what the row count declares
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        for (EquipmentReference& equipment : block.characters[index].equipment) {
            equipment.definitionIndex = kEmptyDefinitionIndex;
        }
    }
    constexpr std::int32_t kOccupied = 1;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        family4::loadout::ResolvedInstances instances{};
        if (!family4::loadout::resolve_instances(account, index, instances)) {
            // mappings aren't published yet; identities still stand and the rows fill later
            continue;
        }
        for (std::size_t item = 0; item < instances.itemCount; ++item) {
            const family4::loadout::SlottedInstance& slotted = instances.items[item];
            if (slotted.equipmentSlot >= block.characters[index].equipment.size()) {
                continue;
            }
            EquipmentReference& reference =
                block.characters[index].equipment[slotted.equipmentSlot];
            reference.definitionIndex = slotted.instance.baseDefinitionIndex;
            reference.value = kOccupied;
        }
    }
    return true;
}

} // namespace dawn::middleware::datagen::roster
