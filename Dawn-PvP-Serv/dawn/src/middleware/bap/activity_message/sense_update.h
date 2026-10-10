#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "activity_patch_epoch_parser.h"
#include "scene_sense.h"
#include "squad_sense.h"
#include "monitor_sense.h"
#include "combatant_sense.h"
#include "native_sense.h"

namespace dawn::middleware::bap::activity_message::sense_update {

inline constexpr std::uint32_t kMessageType = 6;
inline constexpr std::size_t kCapturedBodyBitCapacity = 1152;
// native acknowledgement maximum: 256 top-level + 64 bubbles * 96 keys; entries count key
// occurrences, including one key in several bubble subblocks
inline constexpr std::size_t kRosterEntryCapacity = 256 + 64 * 96;
inline constexpr std::size_t kSenseGroupCapacity = 256;
// Crown/rescue add 239 sense-bearing descriptors and the roster has under 400; leave
// headroom without putting a megabyte on nested parser stacks
inline constexpr std::size_t kSenseObjectCapacity = 512;

inline constexpr std::uint8_t kEpochFieldWidth = 64;
inline constexpr std::uint8_t kLiteralZeroWidth = 1;
inline constexpr std::size_t kOuterBitCapacity = 514'048;

struct RosterEntry final {
    std::uint32_t registryKey{};
    std::int16_t bubble{-1};
    std::uint8_t state{};
    bool active{};
};

struct SenseGroup final {
    std::uint32_t registryKey{};
    std::uint32_t bodyBits{};
    std::uint16_t firstObject{};
    std::uint16_t objectCount{};
};

struct SenseObject final {
    std::uint32_t registryKey{};
    std::uint64_t bodyFirst{};
    std::uint64_t bodySecond{};
    std::uint64_t bodyThird{};
    std::uint64_t bodyFourth{};
    // remaining MSB-first chunks; the last partial chunk is right-aligned like the first three
    std::array<std::uint64_t, kCapturedBodyBitCapacity / 64 - 3> bodyTail{};
    std::uint64_t bodyHash{};
    std::uint32_t bodyBits{};
    std::uint32_t revision{};
    std::uint16_t slotIndex{};
    std::uint16_t bodySetBitCount{};
    std::uint16_t groupOrdinal{};
    std::uint16_t objectOrdinal{};
    std::uint8_t slotType{};
    std::uint32_t nativeSchema{};
    std::uint32_t nativeRevision{};
    bool hasNativeSchema{};
    bool hasRootDelta{};
    native_sense::SourceDelta sourceDelta{};
    std::uint32_t forestActive32{};
    std::uint64_t forestActive64{};
    std::uint32_t forestRevision{};
    std::uint32_t forestSeed{};
    bool hasForestGeneratorState{};
    native::forest_generator_sense::Progress generatorProgress{};
    native::engagement_sense::Output engagement{};
    bool hasGeneratorProgress{};
    scene_sense::Output sceneOutput{};
    bool hasSceneOutput{};
    squad_sense::Output squadOutput{};
    bool hasSquadOutput{};
    monitor_sense::Output monitorOutput{};
    bool hasMonitorOutput{};
    combatant_sense::Output combatantOutput{};
    bool hasCombatantOutput{};
    object_sense::Output objectOutput{};
    bool hasObjectOutput{};
    bool inferredBodyWidth{};
    bool hasDelta{};
};

struct SenseUpdate final {
    patch_epoch::PatchEpoch epoch{};
    std::array<RosterEntry, kRosterEntryCapacity> rosterEntries{};
    std::array<SenseGroup, kSenseGroupCapacity> groups{};
    std::array<SenseObject, kSenseObjectCapacity> objects{};
    std::uint16_t rosterEntryCount{};
    std::uint16_t topLevelRosterCount{};
    std::uint16_t groupCount{};
    std::uint16_t objectCount{};
    std::uint8_t bubbleBlockCount{};
    std::uint8_t paddingBits{};
    bool hasRosterAcknowledgement{};
};

// body widths exclude the group object-list terminator; unknown schemas, truncation,
// nonzero terminal/padding bits, bad array counts or overflow fail the whole parse
[[nodiscard]] bool parse_sense_update(std::span<const std::byte> input,
                                      SenseUpdate& update,
                                      std::size_t& consumedBits) noexcept;

[[nodiscard]] bool parse_omega_sense_update(std::span<const std::byte> input,
                                      SenseUpdate& update,
                                      std::size_t& consumedBits) noexcept;


} // namespace dawn::middleware::bap::activity_message::sense_update
