#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dawn::middleware::datagen::character_record::layout {

// empty slot is 0xFFFF; 0 would assert definition index 0, a real item
inline constexpr std::uint16_t kEmptyDefinitionIndex = 0xFFFFU;
// engine's no-hash sentinel, the empty value for every definition-hash field
inline constexpr std::uint32_t kNoHash = 0x811C9DC5U;
inline constexpr std::int8_t kEmptyKey = -1;
// empty stat row is -1, not 0, because 0 is a real stat total
inline constexpr std::int32_t kEmptyStatValue = -1;

// authored client-side ids, not extracted data; empty fields carry their own sentinels,
// so the block is written whole
inline constexpr std::array<std::uint8_t, 36> kHeaderBlockBytes{
    0x25, 0x00, 0x05, 0x00, 0x01, 0x00, 0xEA, 0x02, 0x01, 0x03, 0x46, 0x02,
    0x48, 0x03, 0x49, 0x03, 0x4A, 0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0x13, 0x03,
    0xC7, 0x02, 0x00, 0x00, 0xC5, 0x9D, 0x1C, 0x81, 0x01, 0x00, 0x00, 0x00,
};

inline constexpr std::size_t kIdentityByteCount = 3;
inline constexpr std::size_t kHeaderBlockSize = 36;
inline constexpr std::size_t kAbilityBucketCapacity = 12;
inline constexpr std::size_t kBucketHashCapacity = 16;
// flat overflow hash bank after the buckets, holding what no bucket category claims
inline constexpr std::size_t kOverflowHashCapacity = 32;
inline constexpr std::size_t kRenderArtCapacity = 3;
inline constexpr std::size_t kRenderOverlayCapacity = 8;
inline constexpr std::size_t kUnlockPairCapacity = 2;
// the gear render spec searches 6 material pairs by key
inline constexpr std::size_t kMaterialPairCapacity = 6;
// gear art index is the second art slot, arrangement index the third
inline constexpr std::size_t kGearArtSlot = 1;
inline constexpr std::size_t kArtArrangementSlot = 2;
inline constexpr std::size_t kRenderSlotCapacity = 20;
inline constexpr std::size_t kKeyedRowCapacity = 8;
inline constexpr std::size_t kIndexBankCapacity = 48;
inline constexpr std::size_t kSmallIndexBankCapacity = 16;
inline constexpr std::size_t kStatRowCapacity = 32;
inline constexpr std::size_t kWeaponStatTableCapacity = 3;

#pragma pack(push, 1)

struct AbilityBucket {
    std::int8_t kind{kEmptyKey};
    std::array<std::byte, 3> reserved{};
    std::array<std::uint32_t, kBucketHashCapacity> hashes{};
};

struct UnlockPair {
    std::uint32_t definitionHash{kNoHash};
    std::int32_t value{};
};

struct MaterialPair {
    std::int8_t key{kEmptyKey};
    std::array<std::byte, 1> reserved{};
    std::uint16_t value{kEmptyDefinitionIndex};
};

struct RenderEntry {
    std::uint64_t instanceSoid{};
    std::uint16_t definitionIndex{kEmptyDefinitionIndex};
    std::array<std::uint16_t, kRenderArtCapacity> art{};
    std::array<std::uint16_t, kRenderOverlayCapacity> overlays{};
    std::array<UnlockPair, kUnlockPairCapacity> unlockPairs{};
    std::array<MaterialPair, kMaterialPairCapacity> materialPairs{};
};

struct KeyedRow {
    std::int8_t key{kEmptyKey};
    std::array<std::byte, 7> reserved{};
    std::uint64_t value{};
};

struct StatRow {
    std::int8_t key{kEmptyKey};
    std::array<std::byte, 3> reserved{};
    std::int32_t value{};
};

struct Appearance {
    std::int32_t level{};
    float unusedFloatA{};
    float unusedFloatB{};
    // compared as a float by its only reader
    float light{};
    std::array<AbilityBucket, kAbilityBucketCapacity> abilityBuckets{};
    std::array<std::uint32_t, kOverflowHashCapacity> overflowHashes{};
    std::array<RenderEntry, kRenderSlotCapacity> render{};
    std::array<KeyedRow, kKeyedRowCapacity> keyedRows{};
    std::array<std::uint16_t, kIndexBankCapacity> indexBank{};
    std::array<std::uint16_t, kSmallIndexBankCapacity> smallBankA{};
    std::array<std::uint16_t, kSmallIndexBankCapacity> smallBankB{};
    std::array<std::uint16_t, kSmallIndexBankCapacity> smallBankC{};
    std::array<StatRow, kStatRowCapacity> characterStats{};
    std::array<std::array<StatRow, kStatRowCapacity>, kWeaponStatTableCapacity> weaponStats{};
    std::int32_t trailingValue{};
    std::array<std::byte, 4> trailingPadding{};
};

struct Identity {
    std::uint64_t characterSoid{};
    std::array<std::int8_t, kIdentityByteCount> identity{};
    std::array<std::byte, 1> identityPadding{};
    std::array<std::byte, kHeaderBlockSize> headerBlock{};
};

struct Summary {
    float lightFloatA{};
    float lightFloatB{};
    std::int32_t light{};
    // base equipped emblem, read by the native killer-card builder
    std::uint16_t indexA{kEmptyDefinitionIndex};
    // optional emblem variant; overrides the base when present
    std::uint16_t indexB{kEmptyDefinitionIndex};
    std::uint16_t indexC{kEmptyDefinitionIndex};
    std::array<std::byte, 2> reserved{};
    std::int32_t valueA{};
    std::uint32_t hashA{};
    std::uint16_t indexD{kEmptyDefinitionIndex};
    std::array<std::byte, 2> tailPadding{};
};

// only the family-zero record has this. index fields assert definition 0 when left clear,
// so it is written rather than zeroed even though no reader is traced
struct Family0Tail {
    std::uint16_t indexA{kEmptyDefinitionIndex};
    std::uint16_t indexB{kEmptyDefinitionIndex};
    std::uint32_t hash{kNoHash};
    std::uint16_t indexC{kEmptyDefinitionIndex};
    std::uint16_t indexD{kEmptyDefinitionIndex};
    std::uint16_t indexE{kEmptyDefinitionIndex};
    std::uint16_t indexF{kEmptyDefinitionIndex};
    std::uint64_t reserved{};
};

#pragma pack(pop)

inline constexpr std::size_t kIdentitySize = 48;
inline constexpr std::size_t kAppearanceSize = 3'752;
inline constexpr std::size_t kSummarySize = 32;
// each bank is walked by its record stride, so every one must pack to an exact width
inline constexpr std::size_t kAbilityBucketSize = 68;
inline constexpr std::size_t kMaterialPairSize = 4;
inline constexpr std::size_t kUnlockPairSize = 8;
inline constexpr std::size_t kRenderEntrySize = 72;
inline constexpr std::size_t kKeyedRowSize = 16;
inline constexpr std::size_t kStatRowSize = 8;
inline constexpr std::size_t kFamily0TailSize = 24;

// bank positions come from the wire layout, not C++ packing
struct AppearanceOffsets {
    static constexpr std::size_t abilityBuckets = 16;
    static constexpr std::size_t overflowHashes = 832;
    static constexpr std::size_t render = 960;
    static constexpr std::size_t keyedRows = 2'400;
    // trailing index bank; keeps its native empty sentinels
    static constexpr std::size_t indexBank = 2'528;
    static constexpr std::size_t characterStats = 2'720;
    static constexpr std::size_t weaponStats = 2'976;
};

static_assert(sizeof(AbilityBucket) == kAbilityBucketSize);
static_assert(sizeof(MaterialPair) == kMaterialPairSize);
static_assert(sizeof(UnlockPair) == kUnlockPairSize);
static_assert(sizeof(RenderEntry) == kRenderEntrySize);
static_assert(sizeof(KeyedRow) == kKeyedRowSize);
static_assert(sizeof(StatRow) == kStatRowSize);
static_assert(sizeof(Identity) == kIdentitySize);
static_assert(sizeof(Appearance) == kAppearanceSize);
static_assert(sizeof(Summary) == kSummarySize);
static_assert(sizeof(Family0Tail) == kFamily0TailSize);
static_assert(offsetof(Appearance, abilityBuckets) == AppearanceOffsets::abilityBuckets);
static_assert(offsetof(Appearance, overflowHashes) == AppearanceOffsets::overflowHashes);
static_assert(offsetof(Appearance, render) == AppearanceOffsets::render);
static_assert(offsetof(Appearance, keyedRows) == AppearanceOffsets::keyedRows);
static_assert(offsetof(Appearance, indexBank) == AppearanceOffsets::indexBank);
static_assert(offsetof(Appearance, characterStats) == AppearanceOffsets::characterStats);
static_assert(offsetof(Appearance, weaponStats) == AppearanceOffsets::weaponStats);
static_assert(std::is_standard_layout_v<Appearance>);
static_assert(std::is_trivially_copyable_v<Appearance>);

} // namespace dawn::middleware::datagen::character_record::layout
