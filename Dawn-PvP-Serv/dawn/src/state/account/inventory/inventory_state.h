#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace dawn::state::account::inventory {

enum class EquipmentSlot : std::uint8_t {
    kinetic,
    energy,
    heavy,
    helmet,
    gauntlets,
    chest,
    legs,
    classItem,
    ghost,
    vehicle,
    ship,
    subclass,
    clanBanner,
    emblem,
    emote,
    finisher,
    count,
};

inline constexpr std::size_t kEquipmentSlotCount = static_cast<std::size_t>(EquipmentSlot::count);
inline constexpr std::size_t kPlugCapacity = 12;
inline constexpr std::uint32_t kNoDefinitionHash = 0x811C9DC5U;

enum class SocketPolicy : std::uint8_t {
    nativeDefaults,
    authored,
};

struct Sockets {
    SocketPolicy policy{SocketPolicy::nativeDefaults};
    std::array<std::optional<std::uint32_t>, kPlugCapacity> plugs{};
    std::size_t plugCount{};
};

inline constexpr std::size_t kProfileItemCapacity = 701;
inline constexpr std::size_t kProfileActionSourceCapacity = 100;
inline constexpr std::uint64_t kFirstProfileItemInstanceSoid = 0x5000000000000001ULL;
// native item-state bit the client sets to lock an item against destruction; confirmed from three
// observed lock/unlock transitions
inline constexpr std::uint32_t kLockedItemFlag = 0x1;
// the 16 supported equipment buckets reserve 151 native rows; one row per slot can be equipped,
// leaving at most 135 unequipped instances
inline constexpr std::size_t kCharacterItemCapacity = 135;

inline constexpr std::uint32_t kEmoteCollectionDefinitionHash = 3183180185U;
inline constexpr std::size_t kEmoteCollectionSocketLaneCount = 4;
// native slot the Emotes item is equipped under; its real content has no native slot mapping, unlike
// every other character-scoped item, so callers fall back to this via resolve_native_equipment_slot()
inline constexpr std::uint8_t kEmoteCollectionNativeEquipmentSlot =
    static_cast<std::uint8_t>(EquipmentSlot::emote);

[[nodiscard]] bool
resolve_native_equipment_slot(std::uint32_t definitionHash,
                              const std::optional<std::int8_t>& detailEquipmentSlot,
                              std::uint8_t& nativeSlot) noexcept;

struct ProfileItem {
    std::uint64_t instanceSoid{};
    std::uint32_t definitionHash{};
    std::int32_t quantity{};
    std::int32_t mutationSerial{};
};

inline constexpr std::uint64_t kWeaponPickerInstancePrefix = 0x4DB0000000000000ULL;
inline constexpr std::uint64_t kWeaponPickerInstancePrefixMask = 0xFFF0000000000000ULL;
inline constexpr std::uint64_t kAfterWeaponPickerInstanceNamespace =
    (kWeaponPickerInstancePrefix | ~kWeaponPickerInstancePrefixMask) + 1ULL;
[[nodiscard]] constexpr bool is_weapon_picker_instance(std::uint64_t instanceSoid) noexcept {
    return (instanceSoid & kWeaponPickerInstancePrefixMask) == kWeaponPickerInstancePrefix;
}

struct Item {
    std::uint64_t instanceSoid{};
    std::uint32_t definitionHash{};
    std::int32_t level{};
    std::int32_t quantity{};
    std::int32_t mutationSerial{};
    std::uint32_t flags{};
    Sockets sockets;
};

struct CharacterItems {
    std::array<Item, kCharacterItemCapacity> values{};
    std::size_t count{};
};

// the manifest's General bucket (hash 138197802) is the account vault: index 38, 500 rows
inline constexpr std::uint8_t kVaultBucketId = 38;
inline constexpr std::size_t kVaultItemCapacity = 500;

struct VaultItems {
    std::array<Item, kVaultItemCapacity> values{};
    std::size_t count{};
};

struct Equipment {
    std::array<std::optional<Item>, kEquipmentSlotCount> slots{};
};

[[nodiscard]] std::optional<EquipmentSlot> slot_from_name(std::string_view name) noexcept;

[[nodiscard]] bool valid(const Sockets& sockets) noexcept;

[[nodiscard]] bool valid(const Item& item) noexcept;

[[nodiscard]] bool valid(const Equipment& equipment) noexcept;

[[nodiscard]] bool valid(const CharacterItems& items) noexcept;

[[nodiscard]] bool valid(const VaultItems& items) noexcept;

} // namespace dawn::state::account::inventory
