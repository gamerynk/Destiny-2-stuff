#include "account_state.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace dawn::state::account {
namespace {

inline constexpr std::size_t kIdentityCapacity =
    1 + inventory::kProfileItemCapacity + inventory::kVaultItemCapacity
    + kCharacterCapacity * (1 + inventory::kEquipmentSlotCount + inventory::kCharacterItemCapacity);

[[nodiscard]] bool empty_profile_item(const inventory::ProfileItem& item) noexcept {
    return item.instanceSoid == 0 && item.definitionHash == 0 && item.quantity == 0
           && item.mutationSerial == 0;
}

[[nodiscard]] bool empty_dismantle_reward(const DismantleRewardPolicy& reward) noexcept {
    return reward.definitionHash == 0 && reward.quantity == 0;
}

[[nodiscard]] bool valid_dismantle_rewards(const AccountState& state) noexcept {
    if (state.dismantleRewardCount > state.dismantleRewards.size()) {
        return false;
    }
    for (std::size_t index = 0; index < state.dismantleRewards.size(); ++index) {
        const DismantleRewardPolicy& reward = state.dismantleRewards[index];
        if (index >= state.dismantleRewardCount) {
            if (!empty_dismantle_reward(reward)) {
                return false;
            }
            continue;
        }
        if (reward.definitionHash == inventory::kNoDefinitionHash || reward.quantity <= 0) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (state.dismantleRewards[prior].definitionHash == reward.definitionHash) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool append_identity(std::array<std::uint64_t, kIdentityCapacity>& identities,
                                   std::size_t& count,
                                   std::uint64_t soid) noexcept {
    if (soid == 0 || count >= identities.size()) {
        return false;
    }
    const auto end = identities.cbegin() + static_cast<std::ptrdiff_t>(count);
    if (std::find(identities.cbegin(), end, soid) != end) {
        return false;
    }
    identities[count++] = soid;
    return true;
}

[[nodiscard]] bool valid_impl(const AccountState& state) noexcept {
    if (state.profileItemCount > state.profileItems.size()
        || state.characterCount > state.characters.size()) {
        return false;
    }
    if (!inventory::valid(state.vault)) {
        return false;
    }
    if (state.primarySoid == 0) {
        if (state.profileItemCount != 0 || state.characterCount != 0 || state.vault.count != 0
            || state.dismantleRewardCount != 0 || state.settings.configured
            || state.settings.keyBindings.configured) {
            return false;
        }
        return std::all_of(
                   state.profileItems.cbegin(), state.profileItems.cend(), empty_profile_item)
               && std::all_of(state.dismantleRewards.cbegin(),
                              state.dismantleRewards.cend(),
                              empty_dismantle_reward);
    }
    if (!settings::valid(state.settings) || !valid_dismantle_rewards(state)) {
        return false;
    }

    std::array<std::uint64_t, kIdentityCapacity> identities{};
    std::size_t identityCount = 0;
    if (!append_identity(identities, identityCount, state.primarySoid)) {
        return false;
    }
    for (std::size_t index = 0; index < state.profileItems.size(); ++index) {
        const inventory::ProfileItem& item = state.profileItems[index];
        if (index >= state.profileItemCount) {
            if (!empty_profile_item(item)) {
                return false;
            }
            continue;
        }
        if (item.definitionHash == inventory::kNoDefinitionHash || item.quantity <= 0
            || item.mutationSerial < 0
            || (item.instanceSoid != 0
                && !append_identity(identities, identityCount, item.instanceSoid))) {
            return false;
        }
    }

    bool selected = false;
    for (std::size_t index = 0; index < state.characterCount; ++index) {
        const CharacterState& character = state.characters[index];
        if (!append_identity(identities, identityCount, character.soid)
            || (character.selected && selected) || character.race > CharacterRace::exo
            || character.gender > CharacterGender::female
            || character.characterClass > CharacterClass::warlock
            || !std::isfinite(character.appearanceValue) || !inventory::valid(character.equipment)
            || !inventory::valid(character.inventory)) {
            return false;
        }
        selected = selected || character.selected;
        for (const std::optional<inventory::Item>& item : character.equipment.slots) {
            if (item.has_value()
                && !append_identity(identities, identityCount, item->instanceSoid)) {
                return false;
            }
        }
        for (std::size_t itemIndex = 0; itemIndex < character.inventory.count; ++itemIndex) {
            if (!append_identity(identities,
                                 identityCount,
                                 character.inventory.values[itemIndex].instanceSoid)) {
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < state.vault.count; ++index) {
        if (!append_identity(identities, identityCount, state.vault.values[index].instanceSoid)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool valid(const AccountState& state) noexcept {
    return valid_impl(state);
}

bool valid_authored(const AccountState& state) noexcept {
    return valid_impl(state);
}

std::uint64_t selected_character_soid(const AccountState& state) noexcept {
    const std::size_t count = (std::min)(state.characterCount, state.characters.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (state.characters[index].selected) {
            return state.characters[index].soid;
        }
    }
    return 0;
}

std::uint64_t banner_character_soid(const AccountState& state) noexcept {
    const std::uint64_t selected = selected_character_soid(state);
    if (selected != 0) {
        return selected;
    }
    return state.characterCount == 0 ? 0 : state.characters[0].soid;
}

} // namespace dawn::state::account
