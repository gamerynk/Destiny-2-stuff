#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "../../../../state/build_data/runtime.h"
#include "internal.h"

namespace dawn::middleware::datagen::character_record::appearance {
namespace {

namespace buckets = state::build_data::abilities;

[[nodiscard]] buckets::Selection selection_of(const state::CharacterState& character) noexcept {
    return {character.movementAbilityEntry,
            character.grenadeAbilityEntry,
            character.superAbilityEntry,
            character.meleeAbilityEntry,
            character.classAbilityEntry};
}

} // namespace

bool apply_ability_buckets(const state::CharacterState& character,
                           const family4::loadout::ResolvedInstances& instances,
                           layout::Appearance& appearance) noexcept {
    for (std::size_t index = 0; index < instances.itemCount; ++index) {
        if (instances.items[index].equipmentSlot != kSubclassEquipmentSlot) {
            continue;
        }
        details::Definition detail{};
        buckets::Definition published{};
        if (!state::build_data::find_configured_item_detail(
                instances.items[index].instance.baseDefinitionIndex, detail)
            || !state::build_data::find_ability_buckets(
                detail.socketEntryListIndex, selection_of(character), published)) {
            return false;
        }
        for (std::size_t bucket = 0; bucket < appearance.abilityBuckets.size(); ++bucket) {
            layout::AbilityBucket& target = appearance.abilityBuckets[bucket];
            target.kind = static_cast<std::int8_t>(published.buckets[bucket].kind);
            for (std::size_t entry = 0; entry < published.buckets[bucket].hashCount; ++entry) {
                target.hashes[entry] = published.buckets[bucket].hashes[entry];
            }
            std::array<char, 256> line{};
            int count = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=appearance stage=ability_bucket list=%u sel=%u/%u/%u/%u/%u "
                                      "bucket=%zu kind=%u hashes=",
                                      static_cast<unsigned>(detail.socketEntryListIndex),
                                      static_cast<unsigned>(character.movementAbilityEntry),
                                      static_cast<unsigned>(character.grenadeAbilityEntry),
                                      static_cast<unsigned>(character.superAbilityEntry),
                                      static_cast<unsigned>(character.meleeAbilityEntry),
                                      static_cast<unsigned>(character.classAbilityEntry),
                                      bucket,
                                      static_cast<unsigned>(published.buckets[bucket].kind));
            for (std::size_t entry = 0; count > 0 && entry < published.buckets[bucket].hashCount;
                 ++entry) {
                count += std::snprintf(line.data() + count,
                                       line.size() - static_cast<std::size_t>(count),
                                       "%08X ",
                                       published.buckets[bucket].hashes[entry]);
            }
            if (count > 0) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::info,
                                 {line.data(), static_cast<std::size_t>(count)});
            }
        }
        {
            std::array<char, 480> line{};
            int count = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=appearance stage=ability_overflow list=%u count=%u hashes=",
                                      static_cast<unsigned>(detail.socketEntryListIndex),
                                      static_cast<unsigned>(published.overflowCount));
            for (std::size_t entry = 0; count > 0 && entry < published.overflowCount; ++entry) {
                count += std::snprintf(line.data() + count,
                                       line.size() - static_cast<std::size_t>(count),
                                       "%08X ",
                                       published.overflow[entry]);
            }
            if (count > 0) {
                core::log::write(core::log::Channel::server,
                                 core::log::Level::info,
                                 {line.data(), static_cast<std::size_t>(count)});
            }
        }
        // overflow bank takes subclass hashes no bucket category claims; what it leaves stays at
        // the no-hash sentinel for the equipped plug hashes
        for (std::size_t entry = 0; entry < published.overflowCount; ++entry) {
            appearance.overflowHashes[entry] = published.overflow[entry];
        }
        return true;
    }
    // no subclass equipped publishes empty buckets, same as the client computes
    return true;
}

} // namespace dawn::middleware::datagen::character_record::appearance
