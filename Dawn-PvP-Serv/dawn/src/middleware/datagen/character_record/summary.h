#pragma once

#include "../../../state/account/account_state.h"
#include "../family4/loadout/definition.h"
#include "layout.h"

namespace dawn::middleware::datagen::character_record {

[[nodiscard]] inline bool build_summary(const state::CharacterState& character,
                                        const family4::loadout::ResolvedInstances& instances,
                                        std::int32_t light,
                                        layout::Summary& output) noexcept {
    if (instances.itemCount > instances.items.size() || light < 0) {
        return false;
    }
    layout::Summary summary{};
    summary.light = light;
    summary.hashA = layout::kNoHash;
    const auto& emblem = character.equipment.slots[
        static_cast<std::size_t>(state::account::inventory::EquipmentSlot::emblem)];
    if (emblem.has_value()) {
        bool found = false;
        for (std::size_t index = 0; index < instances.itemCount; ++index) {
            const auto& instance = instances.items[index].instance;
            if (instance.instanceSoid != emblem->instanceSoid) {
                continue;
            }
            if (found || instance.instanceSoid == 0
                || instance.baseDefinitionIndex == layout::kEmptyDefinitionIndex
                || instance.baseDefinitionIndex >= instance.bounds.itemDefinitionCount) {
                return false;
            }
            // PC86657 killer extraction reads Family-0 +EE4; 16B7D50 uses this base emblem when the
            // optional variant at +EE6 is absent
            summary.indexA = instance.baseDefinitionIndex;
            found = true;
        }
        if (!found) {
            return false;
        }
    }
    output = summary;
    return true;
}

} // namespace dawn::middleware::datagen::character_record
