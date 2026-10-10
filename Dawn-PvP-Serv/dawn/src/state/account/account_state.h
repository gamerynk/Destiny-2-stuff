#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "inventory/inventory_state.h"
#include "settings/settings_state.h"

namespace dawn::state {

inline constexpr std::size_t kCharacterCapacity = 3;
inline constexpr std::size_t kDismantleRewardPolicyCapacity = 8;

struct DismantleRewardPolicy {
    std::uint32_t definitionHash{};
    std::int32_t quantity{};
};

enum class CharacterRace : std::uint8_t {
    human = 0,
    awoken = 1,
    exo = 2,
};

enum class CharacterGender : std::uint8_t {
    male = 0,
    female = 1,
};

enum class CharacterClass : std::uint8_t {
    titan = 0,
    hunter = 1,
    warlock = 2,
};

// default movement entry; each subclass offers 3, as entries 4, 5 and 6 of its group
inline constexpr std::uint8_t kDefaultMovementAbilityEntry = 4;
inline constexpr std::uint8_t kMaximumMovementAbilityEntry = 63;

inline constexpr std::uint8_t kDefaultGrenadeAbilityEntry = 7;
// default super entry: the lane that carries no plug source and kind 34
inline constexpr std::uint8_t kDefaultSuperAbilityEntry = 10;
inline constexpr std::uint8_t kDefaultMeleeAbilityEntry = 11;
inline constexpr std::uint8_t kDefaultClassAbilityEntry = 2;

struct CharacterState {
    std::uint64_t soid{};
    bool selected{};
    CharacterRace race{CharacterRace::human};
    CharacterGender gender{CharacterGender::male};
    CharacterClass characterClass{CharacterClass::titan};
    std::uint8_t level{};
    bool accepted{};
    bool previewAvailable{};
    float appearanceValue{};
    std::uint32_t lastOrbitedDestination{};
    bool contentBypass{};
    std::uint8_t movementAbilityEntry{kDefaultMovementAbilityEntry};
    std::uint8_t grenadeAbilityEntry{kDefaultGrenadeAbilityEntry};
    std::uint8_t superAbilityEntry{kDefaultSuperAbilityEntry};
    std::uint8_t meleeAbilityEntry{kDefaultMeleeAbilityEntry};
    std::uint8_t classAbilityEntry{kDefaultClassAbilityEntry};
    account::inventory::Equipment equipment;
    account::inventory::CharacterItems inventory;
    std::uint32_t nextInventorySerial{};
    // header struct 0x80807C12 in native layout (36 bytes), as the ws-501 create request carried it; the
    // family-0 and family-3 records write it between the identity triple and the appearance block
    std::array<std::uint8_t, 36> identityHeader{};
    bool hasIdentityHeader{};
};

struct AccountState {
    std::uint64_t primarySoid{};
    std::array<DismantleRewardPolicy, kDismantleRewardPolicyCapacity> dismantleRewards{};
    std::size_t dismantleRewardCount{};
    std::array<account::inventory::ProfileItem, account::inventory::kProfileItemCapacity>
        profileItems{};
    std::size_t profileItemCount{};
    std::array<CharacterState, kCharacterCapacity> characters{};
    std::size_t characterCount{};
    account::settings::AccountSettings settings;
    account::inventory::VaultItems vault;
};

namespace account {

[[nodiscard]] bool valid(const AccountState& state) noexcept;

[[nodiscard]] bool valid_authored(const AccountState& state) noexcept;

[[nodiscard]] std::uint64_t selected_character_soid(const AccountState& state) noexcept;

// published before any pick; a refusal spends the family's only acceptance window, so use the first character
[[nodiscard]] std::uint64_t banner_character_soid(const AccountState& state) noexcept;

} // namespace account

} // namespace dawn::state
