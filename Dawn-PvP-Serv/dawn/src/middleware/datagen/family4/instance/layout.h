#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace dawn::middleware::datagen::family4::instance::layout {

inline constexpr std::size_t kOrdinarySocketCapacity = 12;
inline constexpr std::size_t kSocketAuxiliaryHashCount = 2;
inline constexpr std::size_t kSelectorCapacity = 12;
inline constexpr std::size_t kSocketEntryStateCapacity = 36;
inline constexpr std::size_t kRandomRollByteCount = 8;
inline constexpr std::size_t kCreationEntryCapacity = 6;
inline constexpr std::size_t kCreationDefinitionCount = 4;
inline constexpr std::size_t kTailValueCount = 8;
inline constexpr std::uint16_t kEmptyDefinitionIndex = 0xFFFF;
inline constexpr std::uint32_t kNoDefinitionHash = 0x811C9DC5;
// the signed native lookup ABI exposes 32,768 nonnegative definition indices
inline constexpr std::uint32_t kDefinitionIndexCapacity = 1U << 15U;
inline constexpr std::uint32_t kAllSocketBits = 0xFFFFFFFF;
inline constexpr std::uint8_t kEmptySelectorByte = 0xFF;
// 0 leaves unresolved aux socket hashes inert in the native item record
inline constexpr std::uint32_t kSafeSocketAuxiliaryHash = 0;
// 0 leaves each client-derived socket mask empty before its first rebuild
inline constexpr std::uint32_t kInitialDerivedSocketMask = 0;
inline constexpr std::int32_t kInitialInstanceProgress = 0;
inline constexpr std::int32_t kMinimumItemLevel = 0;
inline constexpr std::uint16_t kInitialLevelCurveX = 0;
inline constexpr std::uint16_t kInitialLevelCapRow = 0;
inline constexpr std::int32_t kEmptyCreationEntryCount = 0;
inline constexpr std::size_t kLevelStateSize = 8;
inline constexpr std::size_t kOrdinarySocketSize = 12;
inline constexpr std::size_t kOrdinarySocketBlockSize = 164;
inline constexpr std::size_t kSocketSelectorSize = 3;
inline constexpr std::size_t kRollStateSize = 100;
inline constexpr std::size_t kCreationEntrySize = 12;
inline constexpr std::size_t kCreationRequestSize = 96;
inline constexpr std::size_t kObjectSize = 416;

#pragma pack(push, 1)

struct LevelState {
    std::int32_t level{};
    std::uint16_t curveX{};
    std::uint16_t capRow{};
};

struct OrdinarySocket {
    std::uint16_t plugDefinitionIndex{};
    std::uint16_t reserved{};
    std::array<std::uint32_t, kSocketAuxiliaryHashCount> auxiliaryHashes{};
};

// a plug source passes only when its set mask holds the socket bit and its clear mask doesn't
struct OrdinarySocketBlock {
    std::array<OrdinarySocket, kOrdinarySocketCapacity> sockets{};
    std::uint32_t activeMask{};
    std::uint32_t definitionUnlockMask{};
    std::uint32_t blockedMask{};
    std::uint32_t expressionUnlockMask{};
    // client ANDs this into activeMask, so a zero blanks every socket's state
    std::uint32_t gateMask{};
};

struct SocketSelector {
    std::uint8_t entry{};
    std::uint8_t group{};
    std::uint8_t element{};
};

struct RollState {
    std::int32_t progress{};
    std::int32_t valueA{};
    std::int32_t valueB{};
    std::uint16_t progressionDefinitionIndex{};
    std::uint16_t reserved{};
    std::array<SocketSelector, kSelectorCapacity> selectors{};
    std::uint16_t socketEntryListIndex{};
    std::array<std::uint8_t, kRandomRollByteCount> randomRoll{};
    std::uint8_t valueC{};
    std::array<std::uint8_t, kSocketEntryStateCapacity> socketEntryStates{};
    std::uint8_t reservedTail{};
};

struct CreationEntry {
    std::array<std::uint16_t, kCreationDefinitionCount> definitionIndices{};
    std::int32_t value{};
};

struct CreationRequest {
    std::uint16_t primaryDefinitionIndex{};
    std::uint16_t reservedA{};
    std::array<std::uint32_t, kSocketAuxiliaryHashCount> definitionHashes{};
    std::int32_t value{};
    std::uint16_t secondaryDefinitionIndex{};
    std::uint16_t reservedB{};
    std::int32_t entryCount{};
    std::array<CreationEntry, kCreationEntryCapacity> entries{};
};

struct Object {
    std::uint64_t instanceSoid{};
    LevelState level{};
    std::uint16_t baseDefinitionIndex{};
    std::uint16_t reserved{};
    OrdinarySocketBlock ordinarySockets{};
    RollState roll{};
    CreationRequest creation{};
    std::uint16_t tailDefinitionIndex{};
    std::uint16_t tailReserved{};
    std::array<std::int32_t, kTailValueCount> tailValues{};
};

#pragma pack(pop)

static_assert(sizeof(LevelState) == kLevelStateSize);
static_assert(sizeof(OrdinarySocket) == kOrdinarySocketSize);
static_assert(sizeof(OrdinarySocketBlock) == kOrdinarySocketBlockSize);
static_assert(sizeof(SocketSelector) == kSocketSelectorSize);
static_assert(sizeof(RollState) == kRollStateSize);
static_assert(sizeof(CreationEntry) == kCreationEntrySize);
static_assert(sizeof(CreationRequest) == kCreationRequestSize);
static_assert(sizeof(Object) == kObjectSize);
static_assert(std::is_trivially_copyable_v<Object>);

} // namespace dawn::middleware::datagen::family4::instance::layout
