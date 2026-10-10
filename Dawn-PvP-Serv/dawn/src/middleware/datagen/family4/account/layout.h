#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "../../roster/queuez_roster.h"
#include "../inventory/layout.h"
#include "../progression/layout.h"
#include "preferences/layout.h"

namespace dawn::middleware::datagen::family4::account::layout {

inline constexpr std::size_t kProfileItemCapacity = 701;
inline constexpr std::size_t kSecondaryItemCapacity = 6;
inline constexpr std::size_t kProgressionCapacity = 127;
inline constexpr std::size_t kPublicityExpiryCapacity = 128;
inline constexpr std::size_t kSeenMessageByteCount = 52;
inline constexpr std::size_t kAccountHeaderTailSize = 128;
inline constexpr std::size_t kRosterSelectionPaddingSize = 8;
inline constexpr std::size_t kSelectionPublicityPaddingSize = 24;
inline constexpr std::size_t kSeenPreferencesPaddingSize = 24;
inline constexpr std::size_t kPreferencesBindingsPaddingSize = 607;
inline constexpr std::size_t kBindingsProfilePaddingSize = 456;
inline constexpr std::size_t kInventoryCountPaddingSize = 4;
// profile inventory observer reads 16 transient mutation descriptors
inline constexpr std::size_t kProfileInventoryChangeRecordCapacity = 16;
// opaque bytes keep the progression bank at its native offset
inline constexpr std::size_t kProfileInventoryProgressionPaddingSize = 512;
inline constexpr std::size_t kAcquiredFlagCapacity = 12'300;
inline constexpr std::size_t kObjectiveValueCapacity = 6'200;
inline constexpr std::size_t kUnlockCharacterCapacity = 4;
inline constexpr std::size_t kCharacterFlagCapacity = 256;
inline constexpr std::size_t kCharacterValueCapacity = 256;
inline constexpr std::size_t kProfileUnlockFlagCapacity = 512;
inline constexpr std::size_t kUnlockProfilePaddingSize = 2'424;
inline constexpr std::size_t kProfileFlagsTailPaddingSize = 21'384;
inline constexpr std::size_t kObjectSize = 96'280;
inline constexpr std::size_t kAccountSoidOffset = 0;
inline constexpr std::size_t kRosterOffset = 136;
inline constexpr std::size_t kSelectedCharacterSoidOffset = 1'832;
inline constexpr std::size_t kPublicityExpiriesOffset = 1'864;
inline constexpr std::size_t kSeenMessagesOffset = 2'888;
inline constexpr std::size_t kPreferencesOffset = 2'964;
// BE4120 copies account+B80 to the profile record; native OK sets its +10 confirmation
inline constexpr std::size_t kProfileSetupCompletedOffset = 0xB90;
inline constexpr std::size_t kBindingsOffset = 3'648;
inline constexpr std::size_t kProfileItemCountOffset = 4'360;
inline constexpr std::size_t kProfileItemsOffset = 4'368;
inline constexpr std::size_t kSecondaryItemCountOffset = 26'800;
inline constexpr std::size_t kSecondaryItemsOffset = 26'808;
inline constexpr std::size_t kProfileInventoryChangesOffset = 27'000;
inline constexpr std::size_t kProgressionsOffset = 27'708;
inline constexpr std::size_t kAcquiredFlagsOffset = 29'740;
inline constexpr std::size_t kObjectiveValuesOffset = 42'040;
inline constexpr std::size_t kCharacterUnlocksOffset = 66'840;
inline constexpr std::size_t kProfileUnlockFlagsOffset = 74'384;

#pragma pack(push, 1)

struct CharacterUnlockBlock {
    std::array<std::uint8_t, kCharacterFlagCapacity> flags{};
    std::array<std::int32_t, kCharacterValueCapacity> values{};
};

struct ProfileInventoryChangeRecord {
    std::uint16_t sequence{};
    std::uint16_t reserved{};
    std::int32_t mutationSerial{};
    // kind 1 follows the ordinary acquisition path
    std::uint8_t kind{};
    std::uint8_t reservedKind{};
    std::uint16_t flags{};
};

// header and fixed record bank at native account-object offset 0x6978
struct ProfileInventoryChangeList {
    std::uint16_t writeSlot{};
    std::uint16_t nextSequence{};
    std::array<ProfileInventoryChangeRecord, kProfileInventoryChangeRecordCapacity> records{};
};

struct Object {
    std::uint64_t accountSoid{};
    std::array<std::byte, kAccountHeaderTailSize> accountHeaderTail{};
    roster::Block roster{};
    std::array<std::byte, kRosterSelectionPaddingSize> rosterSelectionPadding{};
    std::uint64_t selectedCharacterSoid{};
    std::array<std::byte, kSelectionPublicityPaddingSize> selectionPublicityPadding{};
    std::array<std::uint64_t, kPublicityExpiryCapacity> publicityExpiries{};
    std::array<std::byte, kSeenMessageByteCount> seenMessages{};
    std::array<std::byte, kSeenPreferencesPaddingSize> seenPreferencesPadding{};
    preferences::Record preferences{};
    std::array<std::byte, kPreferencesBindingsPaddingSize> preferencesBindingsPadding{};
    preferences::BindingsRecord bindings{};
    std::array<std::byte, kBindingsProfilePaddingSize> bindingsProfilePadding{};
    std::uint32_t profileItemCount{};
    std::array<std::byte, kInventoryCountPaddingSize> profileCountPadding{};
    std::array<inventory::layout::Entry, kProfileItemCapacity> profileItems{};
    std::uint32_t secondaryItemCount{};
    std::array<std::byte, kInventoryCountPaddingSize> secondaryCountPadding{};
    std::array<inventory::layout::Entry, kSecondaryItemCapacity> secondaryItems{};
    ProfileInventoryChangeList profileInventoryChanges{};
    std::array<std::byte, kProfileInventoryProgressionPaddingSize>
        profileInventoryProgressionPadding{};
    std::array<progression::layout::Entry, kProgressionCapacity> progressions{};
    std::array<std::uint8_t, kAcquiredFlagCapacity> acquiredFlags{};
    std::array<std::int32_t, kObjectiveValueCapacity> objectiveValues{};
    std::array<CharacterUnlockBlock, kUnlockCharacterCapacity> characterUnlocks{};
    std::array<std::byte, kUnlockProfilePaddingSize> unlockProfilePadding{};
    std::array<std::uint8_t, kProfileUnlockFlagCapacity> profileUnlockFlags{};
    std::array<std::byte, kProfileFlagsTailPaddingSize> profileFlagsTailPadding{};
};

#pragma pack(pop)

inline constexpr std::size_t kMinimumSize = kObjectSize;

static_assert(sizeof(Object) == kObjectSize);
static_assert(offsetof(Object, accountSoid) == kAccountSoidOffset);
static_assert(offsetof(Object, roster) == kRosterOffset);
static_assert(offsetof(Object, selectedCharacterSoid) == kSelectedCharacterSoidOffset);
static_assert(offsetof(Object, publicityExpiries) == kPublicityExpiriesOffset);
static_assert(offsetof(Object, seenMessages) == kSeenMessagesOffset);
static_assert(offsetof(Object, preferences) == kPreferencesOffset);
static_assert(offsetof(Object, bindings) == kBindingsOffset);
static_assert(offsetof(Object, profileItemCount) == kProfileItemCountOffset);
static_assert(offsetof(Object, profileItems) == kProfileItemsOffset);
static_assert(offsetof(Object, secondaryItemCount) == kSecondaryItemCountOffset);
static_assert(offsetof(Object, secondaryItems) == kSecondaryItemsOffset);
static_assert(offsetof(Object, profileInventoryChanges) == kProfileInventoryChangesOffset);
static_assert(offsetof(Object, progressions) == kProgressionsOffset);
static_assert(offsetof(Object, acquiredFlags) == kAcquiredFlagsOffset);
static_assert(offsetof(Object, objectiveValues) == kObjectiveValuesOffset);
static_assert(offsetof(Object, characterUnlocks) == kCharacterUnlocksOffset);
static_assert(offsetof(Object, profileUnlockFlags) == kProfileUnlockFlagsOffset);
static_assert(sizeof(CharacterUnlockBlock)
              == kCharacterFlagCapacity + kCharacterValueCapacity * sizeof(std::int32_t));
static_assert(sizeof(ProfileInventoryChangeRecord)
              == 3 * sizeof(std::uint16_t) + sizeof(std::int32_t) + 2 * sizeof(std::uint8_t));
static_assert(sizeof(ProfileInventoryChangeList)
              == 2 * sizeof(std::uint16_t)
                     + kProfileInventoryChangeRecordCapacity
                           * sizeof(ProfileInventoryChangeRecord));
static_assert(std::is_standard_layout_v<Object>);
static_assert(std::is_trivially_copyable_v<Object>);

} // namespace dawn::middleware::datagen::family4::account::layout
