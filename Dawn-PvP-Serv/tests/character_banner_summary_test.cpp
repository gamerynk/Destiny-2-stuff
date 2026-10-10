#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "middleware/datagen/character_record/character_record_encoder.h"
#include "middleware/datagen/character_record/appearance/internal.h"
#include "middleware/datagen/family1/family1_encoder.h"

namespace dawn::middleware::datagen::character_record::appearance {
void apply_sentinels(layout::Appearance&) noexcept {}
bool apply_render(const family4::loadout::ResolvedInstances&, state::CharacterClass,
                  layout::Appearance&) noexcept { return true; }
bool apply_stats(const family4::loadout::ResolvedInstances&, std::int32_t,
                 layout::Appearance&) noexcept { return true; }
bool apply_ability_buckets(const state::CharacterState&,
                           const family4::loadout::ResolvedInstances&,
                           layout::Appearance&) noexcept { return true; }
void apply_overflow_hashes(const family4::loadout::ResolvedInstances&,
                            layout::Appearance&) noexcept {}
void apply_perk_banks(const state::CharacterState&, const family4::loadout::ResolvedInstances&,
                      layout::Appearance&) noexcept {}
}

namespace {
template<class T> T read(const std::byte* bytes) {
    T value{};
    std::memcpy(&value, bytes, sizeof value);
    return value;
}
bool require(bool ok, const char* message) {
    if (!ok) std::fprintf(stderr, "FAIL: %s\n", message);
    return ok;
}
}

int main() {
    namespace record = dawn::middleware::datagen::character_record;
    namespace family1 = dawn::middleware::datagen::family1;
    namespace family4 = dawn::middleware::datagen::family4;
    namespace state = dawn::state;
    constexpr auto emblemSlot = static_cast<std::size_t>(
        state::account::inventory::EquipmentSlot::emblem);
    state::CharacterState character{};
    character.soid = 0x9EAA000100100102ULL;
    state::account::inventory::Item equipped{};
    equipped.instanceSoid = 0x4100000000000088ULL;
    character.equipment.slots[emblemSlot] = equipped;
    family4::loadout::ResolvedInstances instances{};
    instances.itemCount = 3;
    for (std::size_t i = 0; i < instances.itemCount; ++i) {
        instances.items[i].instance.instanceSoid = equipped.instanceSoid + i;
        instances.items[i].instance.bounds.itemDefinitionCount = 10000;
        instances.items[i].instance.baseDefinitionIndex = static_cast<std::uint16_t>(1273 + i);
    }
    std::swap(instances.items[0], instances.items[2]);
    instances.items[2].equipmentSlot = 17;
    std::array<std::byte, record::kFamily0RecordSize> zero{};
    std::array<std::byte, record::kFamily3RecordSize> three{};
    bool passed = true;
    for (const auto pair : {std::pair{std::uint16_t{1273}, 1060},
                            std::pair{std::uint16_t{2368}, 1047}}) {
        instances.items[2].instance.baseDefinitionIndex = pair.first;
        passed &= require(record::encode_family0(character, instances, pair.second, zero),
                          "Family-0 actual equipped emblem encodes");
        passed &= require(record::encode_family3(character, instances, pair.second, three),
                          "Family-3 actual equipped emblem encodes");
        passed &= require(read<std::uint16_t>(zero.data() + 0xEE4) == pair.first
                              && read<std::uint16_t>(three.data() + 0xF1C) == pair.first,
                          "Native base-emblem offsets reflect equipped selection");
        passed &= require(read<std::int32_t>(zero.data() + 0xEE0) == pair.second
                              && read<float>(zero.data() + 0x3C) == pair.second,
                          "Native summary and appearance retain evaluated equipment Power");
        passed &= require(read<std::uint16_t>(zero.data() + 0xEE6) == 0xFFFF
                              && read<std::uint16_t>(zero.data() + 0xEE8) == 0xFFFF,
                          "Absent variant and auxiliary emblem retain native sentinels");
        std::array<family4::progression::layout::Entry, family1::kProgressionCapacity> progression{};
        family4::loadout::ResolvedLoadout loadout{};
        std::array<std::byte, family1::kBodySize> one{};
        const std::span<const std::byte> presentation(zero);
        passed &= require(family1::encode_body(0x9EAA000100100100ULL,
            presentation.first(0x30), progression, loadout,
            presentation.subspan(0x30, 0xEA8), presentation.subspan(0xED8, 0x20), one),
            "Family-1 public profile retains shared summary");
        passed &= require(read<std::uint16_t>(one.data() + 0x1B34) == pair.first
                              && read<std::int32_t>(one.data() + 0x1B30) == pair.second,
                          "Family-1 public emblem and Power use the same selected record");
    }
    const auto before = zero;
    instances.items[2].instance.instanceSoid++;
    passed &= require(!record::encode_family0(character, instances, 1060, zero) && zero == before,
                      "Unresolved equipped emblem fails without replacing serialized data");
    instances.items[2].instance.instanceSoid = equipped.instanceSoid;
    instances.items[2].instance.baseDefinitionIndex = 10000;
    passed &= require(!record::encode_family0(character, instances, 1060, zero) && zero == before,
                      "Out-of-range emblem definition is rejected atomically");
    character.equipment.slots[emblemSlot].reset();
    passed &= require(record::encode_family0(character, instances, 1060, zero)
                          && read<std::uint16_t>(zero.data() + 0xEE4) == 0xFFFF,
                      "No equipped emblem keeps the empty sentinel");
    if (passed) std::puts("PASS: native character banner summary");
    return passed ? 0 : 1;
}
