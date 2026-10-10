#include "configured_equipment_identity.h"

#include <limits>
#include <optional>

namespace dawn::state::runtime::equipment {
namespace {

constexpr std::uint64_t kEquipmentHashOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kEquipmentHashPrime = 1099511628211ULL;
constexpr std::uint8_t kAbsentItemMarker = 0;
// marker 1 means present, even when the authored definition hash is 0
constexpr std::uint8_t kPresentItemMarker = 1;

void mix_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kEquipmentHashPrime;
}

void mix_value(std::uint64_t& hash, std::uint16_t value) noexcept {
    for (std::size_t byteIndex = 0; byteIndex < sizeof value; ++byteIndex) {
        const std::size_t shift = byteIndex * (std::numeric_limits<std::uint8_t>::digits);
        mix_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

void mix_value(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (std::size_t byteIndex = 0; byteIndex < sizeof value; ++byteIndex) {
        const std::size_t shift = byteIndex * (std::numeric_limits<std::uint8_t>::digits);
        mix_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

// extraction reads a detail row for each authored plug, so a changed plug must rebuild
void mix_sockets(std::uint64_t& hash, const account::inventory::Sockets& sockets) noexcept {
    mix_byte(hash, static_cast<std::uint8_t>(sockets.policy));
    mix_byte(hash, static_cast<std::uint8_t>(sockets.plugCount));
    for (std::size_t lane = 0; lane < sockets.plugCount && lane < sockets.plugs.size(); ++lane) {
        if (!sockets.plugs[lane].has_value()) {
            mix_byte(hash, kAbsentItemMarker);
            continue;
        }
        mix_byte(hash, kPresentItemMarker);
        mix_value(hash, *sockets.plugs[lane]);
    }
}

void mix_item(std::uint64_t& hash, const account::inventory::Item& item) noexcept {
    // SOIDs, quantity, gates and secrets stay outside build identity
    mix_value(hash, item.definitionHash);
    mix_value(hash, static_cast<std::uint32_t>(item.level));
    mix_sockets(hash, item.sockets);
}

// ability bucket rows are keyed by these 5 entries, so a changed pick must rebuild
void mix_ability_selection(std::uint64_t& hash, const CharacterState& character) noexcept {
    mix_byte(hash, character.movementAbilityEntry);
    mix_byte(hash, character.grenadeAbilityEntry);
    mix_byte(hash, character.superAbilityEntry);
    mix_byte(hash, character.meleeAbilityEntry);
    mix_byte(hash, character.classAbilityEntry);
}

} // namespace

std::uint64_t configured_hash(const AccountState& accountState) noexcept {
    std::uint64_t hash = kEquipmentHashOffsetBasis;
    mix_byte(hash, static_cast<std::uint8_t>(accountState.characterCount));
    for (std::size_t characterIndex = 0; characterIndex < accountState.characterCount;
         ++characterIndex) {
        const CharacterState& character = accountState.characters[characterIndex];
        mix_ability_selection(hash, character);
        for (const std::optional<account::inventory::Item>& item : character.equipment.slots) {
            if (!item.has_value()) {
                mix_byte(hash, kAbsentItemMarker);
                continue;
            }
            mix_byte(hash, kPresentItemMarker);
            mix_item(hash, *item);
        }
        static_assert(account::inventory::kCharacterItemCapacity
                      <= (std::numeric_limits<std::uint8_t>::max)());
        mix_byte(hash, static_cast<std::uint8_t>(character.inventory.count));
        for (std::size_t itemIndex = 0; itemIndex < character.inventory.count; ++itemIndex) {
            mix_item(hash, character.inventory.values[itemIndex]);
        }
    }
    return hash;
}

} // namespace dawn::state::runtime::equipment
