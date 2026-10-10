#include <algorithm>
#include <array>
#include <limits>

#include "../../encoding/bit_reader.h"
#include "../../encoding/byte_order.h"
#include "sense_update.h"
#include "native_sense.h"

namespace dawn::middleware::bap::activity_message::sense_update {
namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kPresenceWidth = 1;
constexpr std::uint8_t kKeyWidth = 32;
constexpr std::uint8_t kSlotTypeWidth = 7;
constexpr std::uint8_t kSlotIndexWidth = 16;
constexpr std::uint32_t kSlotTypeBias = 1;
constexpr std::uint32_t kSlotIndexBias = 32768;
constexpr std::size_t kObjectHeaderBits =
    kPresenceWidth + kKeyWidth + kSlotTypeWidth + kSlotIndexWidth;

constexpr std::uint8_t kTopCountWidth = 9;
constexpr std::size_t kTopMaskWords = 8;
constexpr std::uint8_t kBubbleCountWidth = 7;
constexpr std::size_t kBubbleMaskWords = 3;
constexpr std::uint32_t kBubbleKeyBias = 0x80000000U;
constexpr std::uint32_t kMaximumBubble = 63;
constexpr std::size_t kMaximumTopKeys = kTopMaskWords * 32;
constexpr std::size_t kMaximumBubbleKeys = kBubbleMaskWords * 32;

constexpr std::uint8_t kSlotTypeForestGenerator = 37;
constexpr std::uint8_t kSlotTypeScene = 43;
constexpr std::uint8_t kSlotTypeSquadSource = 1;
constexpr std::size_t kForestGeneratorBodyBits = 1783;

// decoder for the captured 2763EC97/37/1 fixed form, not a general Forest schema. Native
// 4D8490 appends revision32, so bits 1751..1782 are revision, not padding
constexpr std::size_t kForestActive32Bit = 990;
constexpr std::size_t kForestActive32Count = 32;
constexpr std::size_t kForestActive64Bit = 1246;
constexpr std::size_t kForestBooleanStride = 8;
constexpr std::size_t kForestRevisionBit = kForestGeneratorBodyBits - 32;

constexpr std::uint64_t kProjectFnvBasis = 1469598103934665603ULL;
constexpr std::uint64_t kProjectFnvPrime = 1099511628211ULL;

[[nodiscard]] bool read_expected(bits::Reader& reader,
                                 std::uint8_t width,
                                 std::uint64_t expected) noexcept {
    std::uint64_t value = 0;
    return reader.read(width, value) && value == expected;
}

[[nodiscard]] bool read_masks(bits::Reader& reader,
                              std::span<std::uint32_t> masks) noexcept {
    for (std::uint32_t& mask : masks) {
        std::uint64_t value = 0;
        if (!reader.read(32, value)) {
            return false;
        }
        mask = static_cast<std::uint32_t>(value);
    }
    return true;
}

[[nodiscard]] bool append_roster_entry(SenseUpdate& update,
                                       std::uint32_t key,
                                       std::int16_t bubble,
                                       bool active) noexcept {
    if (update.rosterEntryCount >= update.rosterEntries.size()) {
        return false;
    }
    RosterEntry& entry = update.rosterEntries[update.rosterEntryCount++];
    entry.registryKey = key;
    entry.bubble = bubble;
    entry.active = active;
    return true;
}

[[nodiscard]] bool parse_roster_acknowledgement(bits::Reader& reader,
                                                SenseUpdate& update) noexcept {
    std::uint64_t present = 0;
    if (!reader.read(kPresenceWidth, present)) {
        return false;
    }
    if (present == 0) {
        return true;
    }
    update.hasRosterAcknowledgement = true;
    if (!read_expected(reader, kPresenceWidth, 1)
        || !read_expected(reader, kPresenceWidth, 1)) {
        return false;
    }

    std::uint64_t topCountValue = 0;
    if (!reader.read(kTopCountWidth, topCountValue) || topCountValue > kMaximumTopKeys
        || topCountValue > update.rosterEntries.size()) {
        return false;
    }
    const auto topCount = static_cast<std::size_t>(topCountValue);
    update.topLevelRosterCount = static_cast<std::uint16_t>(topCount);
    for (std::size_t index = 0; index < topCount; ++index) {
        std::uint64_t key = 0;
        if (!reader.read(kKeyWidth, key)
            || !append_roster_entry(update, static_cast<std::uint32_t>(key), -1, false)) {
            return false;
        }
    }
    if (!read_expected(reader, kPresenceWidth, 1)) {
        return false;
    }
    std::array<std::uint32_t, kTopMaskWords> topMasks{};
    if (!read_masks(reader, topMasks) || !read_expected(reader, kPresenceWidth, 1)) {
        return false;
    }
    std::uint64_t topStateCount = 0;
    if (!reader.read(kTopCountWidth, topStateCount) || topStateCount != topCount) {
        return false;
    }
    for (std::size_t index = 0; index < topCount; ++index) {
        std::uint64_t state = 0;
        if (!reader.read(8, state)) {
            return false;
        }
        RosterEntry& entry = update.rosterEntries[index];
        entry.state = static_cast<std::uint8_t>(state);
        entry.active = ((topMasks[index / 32] >> (index % 32)) & 1U) != 0U;
    }

    std::uint64_t hasBubbles = 0;
    if (!reader.read(kPresenceWidth, hasBubbles)) {
        return false;
    }
    if (hasBubbles == 0) {
        return true;
    }
    std::uint64_t bubbleCountValue = 0;
    if (!reader.read(kBubbleCountWidth, bubbleCountValue)
        || bubbleCountValue > kMaximumBubble + 1U) {
        return false;
    }
    update.bubbleBlockCount = static_cast<std::uint8_t>(bubbleCountValue);
    for (std::size_t block = 0; block < bubbleCountValue; ++block) {
        std::uint64_t encodedBubble = 0;
        std::uint64_t keyCountValue = 0;
        if (!read_expected(reader, kPresenceWidth, 1)
            || !reader.read(kKeyWidth, encodedBubble) || encodedBubble < kBubbleKeyBias
            || encodedBubble - kBubbleKeyBias > kMaximumBubble
            || !read_expected(reader, kPresenceWidth, 1)
            || !read_expected(reader, kPresenceWidth, 1)
            || !reader.read(kBubbleCountWidth, keyCountValue)
            || keyCountValue > kMaximumBubbleKeys
            || keyCountValue > update.rosterEntries.size() - update.rosterEntryCount) {
            return false;
        }
        const auto bubble = static_cast<std::int16_t>(encodedBubble - kBubbleKeyBias);
        const std::size_t keyCount = static_cast<std::size_t>(keyCountValue);
        const std::size_t firstEntry = update.rosterEntryCount;
        for (std::size_t index = 0; index < keyCount; ++index) {
            std::uint64_t key = 0;
            if (!reader.read(kKeyWidth, key)
                || !append_roster_entry(
                    update, static_cast<std::uint32_t>(key), bubble, false)) {
                return false;
            }
        }
        if (!read_expected(reader, kPresenceWidth, 1)) {
            return false;
        }
        std::array<std::uint32_t, kBubbleMaskWords> masks{};
        if (!read_masks(reader, masks) || !read_expected(reader, kPresenceWidth, 1)) {
            return false;
        }
        std::uint64_t stateCount = 0;
        if (!reader.read(kBubbleCountWidth, stateCount) || stateCount != keyCount) {
            return false;
        }
        for (std::size_t index = 0; index < keyCount; ++index) {
            std::uint64_t state = 0;
            if (!reader.read(8, state)) {
                return false;
            }
            RosterEntry& entry = update.rosterEntries[firstEntry + index];
            entry.state = static_cast<std::uint8_t>(state);
            entry.active = ((masks[index / 32] >> (index % 32)) & 1U) != 0U;
        }
    }
    return true;
}

[[nodiscard]] bool plausible_body_boundary(const bits::Reader& bodyReader,
                                            std::size_t bodyBits,
                                            std::size_t groupBitsRemaining,
                                            std::uint32_t groupKey) noexcept {
    if (bodyBits >= groupBitsRemaining) return false;
    bits::Reader probe = bodyReader;
    std::uint64_t present{}, key{}, encodedType{}, encodedIndex{};
    if (!probe.skip(bodyBits) || !probe.read(1,present)) return false;
    if (present==0) return groupBitsRemaining-bodyBits==1;
    if (groupBitsRemaining-bodyBits < kObjectHeaderBits+34) return false;
    return probe.read(kKeyWidth,key) && key==groupKey
        && probe.read(kSlotTypeWidth,encodedType) && encodedType>=kSlotTypeBias
        && probe.read(kSlotIndexWidth,encodedIndex) && encodedIndex>=kSlotIndexBias;
}

[[nodiscard]] bool infer_body_bits(const bits::Reader& bodyReader,
                                   std::size_t groupBitsRemaining,
                                   std::uint32_t groupKey,
                                   std::size_t& bodyBits) noexcept {
    if (groupBitsRemaining<34) return false;
    for (std::size_t candidate=33; candidate+kObjectHeaderBits+34<=groupBitsRemaining; ++candidate) {
        if (plausible_body_boundary(bodyReader,candidate,groupBitsRemaining,groupKey)) {
            bodyBits=candidate; return true;
        }
    }
    bodyBits=groupBitsRemaining-1;
    return plausible_body_boundary(bodyReader,bodyBits,groupBitsRemaining,groupKey);
}

[[nodiscard]] bool resolve_body_bits(const bits::Reader& reader,
                                     std::uint8_t slotType,
                                     std::size_t groupBitsRemaining,
                                     std::uint32_t groupKey,
                                     std::size_t& bodyBits,
                                     bool& inferred) noexcept {
    inferred=false;
    if (native_sense::schema(slotType)!=0) {
        bits::Reader probe=reader;
        native_sense::Output output{};
        return native_sense::read(probe,slotType,output,bodyBits)
            && plausible_body_boundary(reader,bodyBits,groupBitsRemaining,groupKey);
    }
    inferred=true;
    return infer_body_bits(reader,groupBitsRemaining,groupKey,bodyBits);
}

[[nodiscard]] bool read_forest_generator_body(bits::Reader& reader,
                                               SenseObject& object) noexcept {
    std::uint64_t hash = kProjectFnvBasis;
    for (std::size_t shift = 0; shift < 32; shift += 8) {
        hash ^= (kForestGeneratorBodyBits >> shift) & 0xFFU;
        hash *= kProjectFnvPrime;
    }

    std::array<std::uint64_t*, 4> captured{{&object.bodyFirst,
                                            &object.bodySecond,
                                            &object.bodyThird,
                                            &object.bodyFourth}};
    std::uint16_t setBits = 0;
    for (std::size_t position = 0; position < kForestGeneratorBodyBits; ++position) {
        std::uint64_t bit = 0;
        if (!reader.read(1, bit)) {
            return false;
        }
        hash ^= bit;
        hash *= kProjectFnvPrime;
        setBits = static_cast<std::uint16_t>(setBits + (bit != 0 ? 1U : 0U));
        if (position < 256U) {
            std::uint64_t& field = *captured[position / 64];
            field = (field << 1U) | bit;
        }

        if (position >= kForestActive32Bit
            && position < kForestActive32Bit
                              + kForestActive32Count * kForestBooleanStride) {
            const std::size_t relative = position - kForestActive32Bit;
            if (bit != 0 && relative % kForestBooleanStride != 0) {
                return false;
            }
            if (bit != 0) {
                object.forestActive32 |= 1U << (relative / kForestBooleanStride);
            }
        } else if (position >= kForestActive64Bit
                   && position < kForestRevisionBit) {
            const std::size_t relative = position - kForestActive64Bit;
            if (bit != 0 && relative % kForestBooleanStride != 0) {
                return false;
            }
            if (bit != 0) {
                object.forestActive64 |= 1ULL << (relative / kForestBooleanStride);
            }
        } else if (position >= kForestRevisionBit) {
            object.forestRevision = (object.forestRevision << 1U)
                                    | static_cast<std::uint32_t>(bit);
        }
    }
    object.bodyBits = static_cast<std::uint32_t>(kForestGeneratorBodyBits);
    object.bodySetBitCount = setBits;
    object.bodyHash = hash;
    object.hasForestGeneratorState = true;
    return true;
}

[[nodiscard]] bool read_object_body(bits::Reader& reader,
                                    std::size_t bodyBits,
                                    SenseObject& object) noexcept {
    if (native_sense::schema(object.slotType)!=0) {
        bits::Reader probe=reader;
        native_sense::Output output{};
        std::size_t decodedBits{};
        if (!native_sense::read(probe,object.slotType,output,decodedBits) || decodedBits!=bodyBits)
            return false;
        object.nativeSchema=output.schema;
        object.nativeRevision=output.revision;
        object.hasNativeSchema=true;
        object.hasRootDelta=output.root;
        if(object.slotType==37 && output.root) {
            object.forestSeed=output.generatorSeed;
            object.forestActive32=output.generatorRegions;
            object.forestActive64=output.generatorGroups;
            object.forestRevision=output.revision;
            object.hasForestGeneratorState=true;
        }
        if (object.slotType==2 && output.root) {
            object.combatantOutput=output.combatant;
            object.hasCombatantOutput=true;
        }
        if (object.slotType==4 && output.root) {
            object.objectOutput=output.object;
            object.hasObjectOutput=true;
        }
        if (object.slotType==30 && output.root) {
            object.monitorOutput=output.monitor;
            object.hasMonitorOutput=true;
        }
        object.sourceDelta=output.source;
        object.engagement=output.engagement;
        object.revision=output.revision;
        object.hasDelta=output.root;
        if(object.slotType==kSlotTypeForestGenerator) {
            object.generatorProgress=output.generator;
            object.hasGeneratorProgress=output.root;
        }
        if (object.slotType==kSlotTypeScene) {
            object.sceneOutput=output.scene;
            object.hasSceneOutput=true;
        }
        if (object.slotType==kSlotTypeSquadSource && output.root) {
            object.squadOutput=output.squad;
            object.hasSquadOutput=true;
        }
    }
    if (!object.hasNativeSchema && object.registryKey == 0x2763EC97U && object.slotIndex == 1
        && object.slotType == kSlotTypeForestGenerator
        && bodyBits == kForestGeneratorBodyBits) {
        return read_forest_generator_body(reader, object);
    }
    std::array<std::uint64_t*, 4> fields{{&object.bodyFirst,
                                          &object.bodySecond,
                                          &object.bodyThird,
                                          &object.bodyFourth}};
    std::uint64_t hash = kProjectFnvBasis;
    for (std::size_t shift = 0; shift < 32; shift += 8) {
        hash ^= (bodyBits >> shift) & 0xFFU;
        hash *= kProjectFnvPrime;
    }
    std::uint32_t setBits = 0;
    for (std::size_t position = 0; position < bodyBits; ++position) {
        std::uint64_t bit = 0;
        if (!reader.read(1, bit)) {
            return false;
        }
        hash ^= bit;
        hash *= kProjectFnvPrime;
        setBits += bit != 0 ? 1U : 0U;
        if (position < 256U) {
            std::uint64_t& field = *fields[position / 64U];
            field = (field << 1U) | bit;
        }
    }
    object.bodyBits = static_cast<std::uint32_t>(bodyBits);
    object.bodySetBitCount = static_cast<std::uint16_t>((std::min)(
        setBits, static_cast<std::uint32_t>((std::numeric_limits<std::uint16_t>::max)())));
    object.bodyHash = hash;
    return true;
}

[[nodiscard]] bool parse_groups(bits::Reader& reader, SenseUpdate& update) noexcept {
    for (;;) {
        std::uint64_t present = 0;
        if (!reader.read(kPresenceWidth, present)) {
            return false;
        }
        if (present == 0) {
            return true;
        }
        if (update.groupCount >= update.groups.size()) {
            return false;
        }
        std::uint64_t keyValue = 0;
        std::uint64_t groupBitsValue = 0;
        if (!reader.read(kKeyWidth, keyValue) || !reader.read(kKeyWidth, groupBitsValue)
            || groupBitsValue == 0 || groupBitsValue > reader.remaining_bits()
            || groupBitsValue > (std::numeric_limits<std::uint32_t>::max)()) {
            return false;
        }
        SenseGroup& group = update.groups[update.groupCount];
        group.registryKey = static_cast<std::uint32_t>(keyValue);
        group.bodyBits = static_cast<std::uint32_t>(groupBitsValue);
        group.firstObject = update.objectCount;
        const std::size_t groupStartRemaining = reader.remaining_bits();
        for (;;) {
            const std::size_t consumed=groupStartRemaining-reader.remaining_bits();
            if (consumed>=group.bodyBits) return false;
            const std::size_t groupRemaining=group.bodyBits-consumed;
            std::uint64_t objectPresent{},objectKey{},encodedType{},encodedIndex{};
            if (!reader.read(1,objectPresent)) return false;
            if (objectPresent==0) {
                if (groupRemaining!=1) return false;
                break;
            }
            if (groupRemaining<kObjectHeaderBits+34 || update.objectCount>=update.objects.size()
                || !reader.read(kKeyWidth,objectKey) || objectKey!=group.registryKey
                || !reader.read(kSlotTypeWidth,encodedType) || encodedType<kSlotTypeBias
                || !reader.read(kSlotIndexWidth,encodedIndex) || encodedIndex<kSlotIndexBias)
                return false;
            SenseObject& object = update.objects[update.objectCount];
            object.registryKey = group.registryKey;
            object.slotType = static_cast<std::uint8_t>(encodedType - kSlotTypeBias);
            object.slotIndex = static_cast<std::uint16_t>(encodedIndex - kSlotIndexBias);
            object.groupOrdinal = update.groupCount;
            object.objectOrdinal = group.objectCount;
            const std::size_t afterHeader = groupStartRemaining - reader.remaining_bits();
            if (afterHeader > group.bodyBits) {
                return false;
            }
            std::size_t bodyBits = 0;
            bool inferredBodyWidth = false;
            if (!resolve_body_bits(reader,
                                   object.slotType,
                                   group.bodyBits - afterHeader,
                                   group.registryKey,
                                   bodyBits,
                                   inferredBodyWidth)
                || !read_object_body(reader, bodyBits, object)) {
                return false;
            }
            object.inferredBodyWidth = inferredBodyWidth;
            ++update.objectCount;
            ++group.objectCount;
        }
        if (groupStartRemaining - reader.remaining_bits() != group.bodyBits) {
            return false;
        }
        ++update.groupCount;
    }
}

[[nodiscard]] bool parse_recovered(std::span<const std::byte> input,
                                   SenseUpdate& update,
                                   std::size_t& consumedBits) noexcept {
    bits::Reader reader(input);
    std::uint64_t literal = 0;
    if (!reader.read(kEpochFieldWidth, update.epoch.first)
        || !reader.read(kEpochFieldWidth, update.epoch.second)
        || !reader.read(kLiteralZeroWidth, literal) || literal != 0
        || !parse_roster_acknowledgement(reader, update) || !parse_groups(reader, update)
        || !read_expected(reader, kPresenceWidth, 0)) {
        return false;
    }

    const std::size_t paddingBits = reader.remaining_bits();
    if (paddingBits > encoding::kBitsPerByte - 1U) {
        return false;
    }
    consumedBits = input.size() * encoding::kBitsPerByte - paddingBits;
    std::uint64_t padding = 0;
    if (!reader.read(static_cast<std::uint8_t>(paddingBits), padding) || padding != 0) {
        return false;
    }
    update.paddingBits = static_cast<std::uint8_t>(paddingBits);
    return true;
}

} // namespace

void reset_update(SenseUpdate& update) noexcept {
    for (auto& entry : update.rosterEntries) entry = RosterEntry{};
    for (auto& group : update.groups) group = SenseGroup{};
    for (auto& object : update.objects) object = SenseObject{};
    update.epoch = {};
    update.rosterEntryCount = update.topLevelRosterCount = update.groupCount = update.objectCount = 0;
    update.bubbleBlockCount = update.paddingBits = 0;
    update.hasRosterAcknowledgement = false;
}

bool parse_sense_update(std::span<const std::byte> input,
                        SenseUpdate& update,
                        std::size_t& consumedBits) noexcept {
    reset_update(update);
    consumedBits = 0;
    if (input.size() > kOuterBitCapacity / encoding::kBitsPerByte
        || !parse_recovered(input, update, consumedBits)) {
        reset_update(update);
        consumedBits = 0;
        return false;
    }
    return true;
}

} // namespace dawn::middleware::bap::activity_message::sense_update
