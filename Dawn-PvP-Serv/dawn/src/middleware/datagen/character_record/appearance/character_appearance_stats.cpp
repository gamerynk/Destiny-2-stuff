#include <algorithm>
#include <atomic>

#include "../../../../core/logging/log.h"
#include "../../../../state/build_data/runtime.h"
#include "../../../../state/equipment/light/definition.h"
#include "internal.h"

namespace dawn::middleware::datagen::character_record::appearance {
namespace {

namespace constants = state::build_data::constants;

[[nodiscard]] std::int32_t definition_total(std::uint16_t definitionIndex,
                                            std::uint8_t row) noexcept {
    details::Definition detail{};
    if (definitionIndex == details::kUnavailableItemIndex
        || !state::build_data::find_configured_item_detail(definitionIndex, detail)) {
        return 0;
    }
    std::int32_t total = 0;
    const std::size_t stats =
        detail.statCount < detail.stats.size() ? detail.statCount : detail.stats.size();
    for (std::size_t entry = 0; entry < stats; ++entry) {
        if (detail.stats[entry].row == row) {
            total += detail.stats[entry].value;
        }
    }
    return total;
}

// armour keeps only a token value on its own definition, so counting it alone misses its roll
[[nodiscard]] std::int32_t item_total(const Equipped& equipped, std::uint8_t row) noexcept {
    std::int32_t total = definition_total(equipped.definitionIndex, row);
    for (std::size_t lane = 0; lane < equipped.laneCount; ++lane) {
        total += definition_total(equipped.plugs[lane], row);
    }
    return total;
}

[[nodiscard]] bool
collect_rows(const Equipped& equipped, std::span<std::uint8_t> rows, std::size_t& count) noexcept {
    const std::size_t lanes = equipped.laneCount + 1;
    for (std::size_t source = 0; source < lanes; ++source) {
        const std::uint16_t definitionIndex =
            source == 0 ? equipped.definitionIndex : equipped.plugs[source - 1];
        details::Definition detail{};
        if (definitionIndex == details::kUnavailableItemIndex
            || !state::build_data::find_configured_item_detail(definitionIndex, detail)) {
            continue;
        }
        const std::size_t stats =
            detail.statCount < detail.stats.size() ? detail.statCount : detail.stats.size();
        for (std::size_t entry = 0; entry < stats; ++entry) {
            const std::uint8_t row = detail.stats[entry].row;
            if (row == details::kEmptyStatRow) {
                continue;
            }
            if (row >= constants::kStatRowCount) {
                return false;
            }
            if (std::find(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(count), row)
                == rows.begin() + static_cast<std::ptrdiff_t>(count)) {
                if (count >= rows.size()) {
                    return false;
                }
                rows[count++] = row;
            }
        }
    }
    return true;
}

// client writes only contributing rows; a 0 row is omitted, not written as a live key asserting 0
void append(std::uint8_t row,
            std::int32_t value,
            std::array<layout::StatRow, layout::kStatRowCapacity>& table,
            std::size_t& count) noexcept {
    if (value <= 0 || count >= table.size()) {
        return;
    }
    table[count].key = static_cast<std::int8_t>(row);
    table[count].value = value;
    ++count;
}

[[nodiscard]] bool
apply_weapon_table(const Equipped& equipped,
                   std::uint8_t powerRow,
                   std::int32_t power,
                   std::array<layout::StatRow, layout::kStatRowCapacity>& table) noexcept {
    std::array<std::uint8_t, constants::kStatRowCount> rows{powerRow};
    std::size_t rowCount = 1;
    if (!collect_rows(equipped, rows, rowCount)) {
        return false;
    }
    std::sort(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rowCount));
    std::array<layout::StatRow, layout::kStatRowCapacity> staged{};
    std::size_t written = 0;
    for (std::size_t entry = 0; entry < rowCount; ++entry) {
        const std::uint8_t row = rows[entry];
        const std::int32_t value = row == powerRow ? power : item_total(equipped, row);
        if (value > 0 && written >= staged.size()) {
            return false;
        }
        append(row, value, staged, written);
    }
    table = staged;
    return true;
}

std::atomic<bool> g_reportedMissingConstants{};

void report_missing_constants() noexcept {
    if (g_reportedMissingConstants.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    core::log::write(core::log::Channel::server,
                     core::log::Level::warn,
                     "ev=char_stats stage=constants result=unavailable_or_invalid");
}

} // namespace

bool apply_stats(const family4::loadout::ResolvedInstances& instances,
                 std::int32_t light,
                 layout::Appearance& appearance) noexcept {
    constants::InvestmentConstants named{};
    if (!state::build_data::find_investment_constants(named) || !constants::valid(named)) {
        // an absent or unusable weapon Power row silently selects the damage floor
        report_missing_constants();
        return false;
    }
    std::array<std::uint8_t, constants::kCharacterStatRowCount> rows = named.characterStatRows;
    std::sort(rows.begin(), rows.end());

    std::size_t written = 0;
    append(named.lightStatRow, light, appearance.characterStats, written);
    for (const std::uint8_t row : rows) {
        std::int32_t total = 0;
        for (std::size_t index = 0; index < instances.itemCount; ++index) {
            details::Definition detail{};
            Equipped equipped{};
            if (resolve_equipped(instances.items[index], detail, equipped)) {
                total += item_total(equipped, row);
            }
        }
        append(row, total, appearance.characterStats, written);
    }

    for (std::size_t index = 0; index < instances.itemCount; ++index) {
        const std::uint8_t slot = instances.items[index].equipmentSlot;
        const auto weapon = static_cast<std::size_t>(slot - kFirstWeaponSlot);
        if (slot < kFirstWeaponSlot || weapon >= appearance.weaponStats.size()) {
            continue;
        }
        details::Definition detail{};
        Equipped equipped{};
        std::int32_t power = 0;
        if (!resolve_equipped(instances.items[index], detail, equipped)
            || !state::equipment::light::item_power(instances.items[index].instance.level, power)
            || !apply_weapon_table(
                equipped, named.weaponPowerStatRow, power, appearance.weaponStats[weapon])) {
            return false;
        }
    }
    return true;
}

} // namespace dawn::middleware::datagen::character_record::appearance
