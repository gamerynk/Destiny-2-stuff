#include "character_record_encoder.h"

#include <algorithm>
#include <cstring>

#include "appearance/internal.h"
#include "summary.h"

namespace dawn::middleware::datagen::character_record {
namespace {

// reserved block only the family-three record has, between identity and appearance
constexpr std::size_t kFamily3ReservedSize = 56;
constexpr std::size_t kFamily3TailSize = 16;
constexpr std::size_t kFamily0TailSize = 24;
// family-three tail offsets that carry the character-select preview flags
constexpr std::size_t kPreviewFlagOffsets[]{8, 9};

[[nodiscard]] bool build_shared(const state::CharacterState& character,
                                const family4::loadout::ResolvedInstances& instances,
                                std::int32_t light,
                                layout::Identity& identity,
                                layout::Appearance& output) noexcept {
    identity = {};
    identity.characterSoid = character.soid;
    identity.identity = {
        static_cast<std::int8_t>(character.race),
        static_cast<std::int8_t>(character.gender),
        static_cast<std::int8_t>(character.characterClass),
    };
    // player's customization if the character has one, else the authored default.
    // both are the same 36-byte native struct
    std::memcpy(identity.headerBlock.data(),
                character.hasIdentityHeader ? character.identityHeader.data()
                                            : layout::kHeaderBlockBytes.data(),
                layout::kHeaderBlockBytes.size());

    output = {};
    appearance::apply_sentinels(output);
    output.level = character.level;
    output.unusedFloatA = 1.0F;
    output.unusedFloatB = static_cast<float>(light);
    output.light = static_cast<float>(light);
    if (!appearance::apply_render(instances, character.characterClass, output)
        || !appearance::apply_stats(instances, light, output)
        || !appearance::apply_ability_buckets(character, instances, output)) {
        return false;
    }
    // ability buckets claim their overflow slots first; gear hashes take what is left
    appearance::apply_overflow_hashes(instances, output);
    appearance::apply_perk_banks(character, instances, output);
    return true;
}

void copy_record(const layout::Identity& identity,
                 const layout::Appearance& block,
                 const layout::Summary& summary,
                 std::size_t reservedSize,
                 std::size_t tailSize,
                 std::span<std::byte> output) noexcept {
    std::fill(output.begin(), output.end(), std::byte{});
    std::size_t cursor = 0;
    std::memcpy(output.data() + cursor, &identity, sizeof identity);
    cursor += sizeof(identity) + reservedSize;
    std::memcpy(output.data() + cursor, &block, sizeof block);
    cursor += sizeof block;
    std::memcpy(output.data() + cursor, &summary, sizeof summary);
    cursor += sizeof(summary) + tailSize;
    (void)cursor;
}

} // namespace

bool encode_family3(const state::CharacterState& character,
                    const family4::loadout::ResolvedInstances& instances,
                    std::int32_t light,
                    std::span<std::byte> output) noexcept {
    layout::Identity identity{};
    layout::Appearance block{};
    layout::Summary summary{};
    if (output.size() < kFamily3RecordSize
        || !build_summary(character, instances, light, summary)
        || !build_shared(character, instances, light, identity, block)) {
        return false;
    }
    const auto record = output.first(kFamily3RecordSize);
    copy_record(
        identity, block, summary, kFamily3ReservedSize, kFamily3TailSize, record);
    // preview flags sit past the summary; their accessor returns true when the context is missing,
    // so a cleared flag asserts the opposite of the client's own fallback
    const std::size_t tailStart = kFamily3RecordSize - kFamily3TailSize;
    for (const std::size_t offset : kPreviewFlagOffsets) {
        record[tailStart + offset] = character.previewAvailable ? std::byte{1} : std::byte{0};
    }
    return true;
}

bool encode_family0(const state::CharacterState& character,
                    const family4::loadout::ResolvedInstances& instances,
                    std::int32_t light,
                    std::span<std::byte> output) noexcept {
    layout::Identity identity{};
    layout::Appearance block{};
    layout::Summary summary{};
    if (output.size() < kFamily0RecordSize
        || !build_summary(character, instances, light, summary)
        || !build_shared(character, instances, light, identity, block)) {
        return false;
    }
    const auto record = output.first(kFamily0RecordSize);
    copy_record(identity, block, summary, 0, kFamily0TailSize, record);
    const layout::Family0Tail tail{};
    std::memcpy(record.data() + kFamily0RecordSize - kFamily0TailSize, &tail, sizeof tail);
    return true;
}

bool encode_family0_anchor(std::uint64_t accountSoid,
                           std::uint64_t characterSoid,
                           std::span<std::byte> output) noexcept {
    if (output.size() < kFamily0AnchorSize) {
        return false;
    }
    const auto anchor = output.first(kFamily0AnchorSize);
    std::fill(anchor.begin(), anchor.end(), std::byte{});
    std::memcpy(anchor.data(), &accountSoid, sizeof accountSoid);
    std::memcpy(anchor.data() + sizeof accountSoid, &characterSoid, sizeof characterSoid);
    return true;
}

} // namespace dawn::middleware::datagen::character_record
