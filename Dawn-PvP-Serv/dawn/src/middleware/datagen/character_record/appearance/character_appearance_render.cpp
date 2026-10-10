#include <algorithm>

#include "../../../../state/build_data/runtime.h"
#include "internal.h"

namespace dawn::middleware::datagen::character_record::appearance {
namespace {

// art stages in the order the client folds them: base 1, plug 1, plug 0, base 2, plug 2.
// the order decides which 6 keys survive, so stages can't be merged
enum class Stage : std::uint8_t {
    plugOnly = 0,
    base = 1,
    late = 2,
};

// insertion-ordered material keys; only the first 6 reach the record
struct Fold {
    std::array<layout::MaterialPair, layout::kMaterialPairCapacity> pairs{};
    std::size_t count{};
};

// a key after 6 distinct ones can never reach the record, so drop it
void record(Fold& fold, std::int8_t key, std::uint16_t value) noexcept {
    for (std::size_t entry = 0; entry < fold.count; ++entry) {
        if (fold.pairs[entry].key == key) {
            fold.pairs[entry].value = value;
            return;
        }
    }
    if (fold.count < fold.pairs.size()) {
        fold.pairs[fold.count].key = key;
        fold.pairs[fold.count].value = value;
        ++fold.count;
    }
}

void fold_stage(const details::Definition& detail, Stage stage, Fold& fold) noexcept {
    const std::size_t rows = detail.renderOverrideCount < detail.renderOverrides.size()
                                 ? detail.renderOverrideCount
                                 : detail.renderOverrides.size();
    for (std::size_t entry = 0; entry < rows; ++entry) {
        const details::RenderOverride& row = detail.renderOverrides[entry];
        if (row.stage == static_cast<std::uint8_t>(stage)
            && row.key != details::kEmptyOverrideKey) {
            record(fold, row.key, row.value);
        }
    }
}

void fold_plug_stage(const Equipped& equipped, Stage stage, Fold& fold) noexcept {
    for (std::size_t lane = 0; lane < equipped.laneCount; ++lane) {
        details::Definition plug{};
        if (equipped.plugs[lane] == details::kUnavailableItemIndex
            || !state::build_data::find_configured_item_detail(equipped.plugs[lane], plug)) {
            continue;
        }
        fold_stage(plug, stage, fold);
    }
}

void apply_material_pairs(const details::Definition& detail,
                          const Equipped& equipped,
                          layout::RenderEntry& entry) noexcept {
    Fold fold{};
    fold_stage(detail, Stage::base, fold);
    fold_plug_stage(equipped, Stage::base, fold);
    fold_plug_stage(equipped, Stage::plugOnly, fold);
    fold_stage(detail, Stage::late, fold);
    fold_plug_stage(equipped, Stage::late, fold);
    for (std::size_t pair = 0; pair < fold.count; ++pair) {
        entry.materialPairs[pair] = fold.pairs[pair];
    }
}

void apply_plug_art(const Equipped& equipped,
                    state::CharacterClass characterClass,
                    layout::RenderEntry& entry) noexcept {
    for (std::size_t lane = 0; lane < equipped.laneCount; ++lane) {
        details::Definition plug{};
        if (equipped.plugs[lane] == details::kUnavailableItemIndex
            || !state::build_data::find_configured_item_detail(equipped.plugs[lane], plug)) {
            continue;
        }
        // shaders normally carry material rows only; ornaments carry one or both art indices,
        // so the plug replaces only the fields it declares
        if (plug.gearArtIndex != details::kUnavailableArtIndex) {
            entry.art[layout::kGearArtSlot] = plug.gearArtIndex;
        }
        const std::uint16_t overlay = select_art_arrangement(plug, characterClass);
        if (overlay != details::kUnavailableArtIndex) {
            const auto empty = std::find(
                entry.overlays.begin(), entry.overlays.end(), layout::kEmptyDefinitionIndex);
            if (empty != entry.overlays.end()) {
                *empty = overlay;
            }
        }
    }
}

} // namespace

bool resolve_equipped(const family4::loadout::SlottedInstance& slotted,
                      details::Definition& detail,
                      Equipped& equipped) noexcept {
    equipped = {};
    equipped.plugs.fill(details::kUnavailableItemIndex);
    equipped.equipmentSlot = slotted.equipmentSlot;
    equipped.definitionIndex = slotted.instance.baseDefinitionIndex;
    if (!state::build_data::find_configured_item_detail(slotted.instance.baseDefinitionIndex,
                                                        detail)) {
        return false;
    }
    equipped.laneCount = detail.ordinarySocketCount < equipped.plugs.size()
                             ? detail.ordinarySocketCount
                             : equipped.plugs.size();
    const auto& authored = slotted.instance.ordinarySockets.plugs;
    for (std::size_t lane = 0; lane < equipped.laneCount && lane < authored.size(); ++lane) {
        if (authored[lane].has_value()) {
            equipped.plugs[lane] = *authored[lane];
        }
    }
    return true;
}

bool apply_render(const family4::loadout::ResolvedInstances& instances,
                  state::CharacterClass characterClass,
                  layout::Appearance& appearance) noexcept {
    for (std::size_t index = 0; index < instances.itemCount; ++index) {
        const family4::loadout::SlottedInstance& slotted = instances.items[index];
        if (slotted.equipmentSlot >= appearance.render.size()) {
            return false;
        }
        layout::RenderEntry& entry = appearance.render[slotted.equipmentSlot];
        entry.instanceSoid = slotted.instance.instanceSoid;
        entry.definitionIndex = slotted.instance.baseDefinitionIndex;
        details::Definition detail{};
        Equipped equipped{};
        if (!resolve_equipped(slotted, detail, equipped)) {
            continue;
        }
        // both art lookups accept 0, so an item with no art block keeps the empty sentinel
        entry.art[layout::kGearArtSlot] = detail.gearArtIndex;
        entry.art[layout::kArtArrangementSlot] = select_art_arrangement(detail, characterClass);
        apply_plug_art(equipped, characterClass, entry);
        apply_material_pairs(detail, equipped, entry);
    }
    return true;
}

} // namespace dawn::middleware::datagen::character_record::appearance
