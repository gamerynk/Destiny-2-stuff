#include "vendor_catalog.h"

#include <algorithm>
#include <shared_mutex>

#include "../../../core/threading/srw_lock.h"
#include "../table.h"

namespace dawn::state::build_data::vendors {
namespace {

// one lock covers all four tables; a definition names its rows by range, so a reader must
// never see one table replaced and another not
core::threading::SrwLock g_lock;
Table<IndexEntry, kIndexCapacity> g_index;
Table<Definition, kDefinitionCapacity> g_definitions;
Table<SaleRow, kSaleRowCapacity> g_saleRows;
Table<InstalledRow, kInstalledRowCapacity> g_installedRows;

[[nodiscard]] bool array_fits(std::uint16_t count,
                              std::uint32_t base,
                              std::uint32_t classId,
                              std::size_t stride,
                              std::uint32_t expectedClass,
                              std::uint32_t definitionSize) noexcept {
    if (count == 0) {
        return base == 0 && classId == 0;
    }
    if (expectedClass != 0 && classId != expectedClass) {
        return false;
    }
    const std::uint64_t end =
        static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(count) * stride;
    return end <= definitionSize;
}

[[nodiscard]] bool canonical(const Definition& definition,
                             std::span<const IndexEntry> index) noexcept {
    if (definition.index >= index.size() || definition.definitionClass != kDefinitionClass
        || definition.definitionSize == 0
        || definition.definitionHash != index[definition.index].definitionHash
        || definition.definitionTag != index[definition.index].definitionTag) {
        return false;
    }
    return array_fits(definition.installedCount,
                      definition.installedRowBase,
                      definition.installedRowClass,
                      kInstalledRowStride,
                      kInstalledRowClass,
                      definition.definitionSize)
           && array_fits(definition.saleCount,
                         definition.saleRowBase,
                         definition.saleRowClass,
                         kSaleRowStride,
                         kSaleRowClass,
                         definition.definitionSize)
           // the third array has no closed element class, so only its bounds are checked
           && array_fits(definition.thirdCount,
                         definition.thirdRowBase,
                         definition.thirdRowClass,
                         kThirdRowStride,
                         0,
                         definition.definitionSize);
}

[[nodiscard]] bool canonical_sale_rows(const Definition& definition,
                                       std::span<const SaleRow> saleRows) noexcept {
    for (std::size_t row = 0; row < definition.saleCount; ++row) {
        const SaleRow& value = saleRows[definition.saleRowOffset + row];
        // row +100 is bounded by the installed count before any reader strides with it
        const bool selects =
            value.categoryIndex == kAbsentCategoryIndex
            || (value.categoryIndex >= 0 && value.categoryIndex < definition.installedCount);
        if (value.vendorIndex != definition.index || value.rowIndex != row || !selects) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool canonical_installed_rows(const Definition& definition,
                                            std::span<const InstalledRow> installedRows) noexcept {
    for (std::size_t row = 0; row < definition.installedCount; ++row) {
        const InstalledRow& value = installedRows[definition.installedRowOffset + row];
        if (value.vendorIndex != definition.index || value.rowIndex != row) {
            return false;
        }
    }
    return true;
}

template <typename Row>
[[nodiscard]] bool copy_range(std::span<const Row> bank,
                              std::size_t offset,
                              std::size_t rows,
                              std::span<Row> output,
                              std::size_t& count) noexcept {
    count = 0;
    const bool fits =
        output.size() >= rows && offset <= bank.size() && rows <= bank.size() - offset;
    if (fits) {
        std::copy_n(bank.begin() + static_cast<std::ptrdiff_t>(offset), rows, output.begin());
        count = rows;
    }
    return fits;
}

} // namespace

void clear() noexcept {
    const std::lock_guard guard(g_lock);
    g_index.clear();
    g_definitions.clear();
    g_saleRows.clear();
    g_installedRows.clear();
}

bool valid(std::span<const IndexEntry> index,
           std::span<const Definition> definitions,
           std::span<const SaleRow> saleRows,
           std::span<const InstalledRow> installedRows) noexcept {
    if (index.empty() || index.size() > kIndexCapacity || definitions.size() > kDefinitionCapacity
        || saleRows.size() > kSaleRowCapacity || installedRows.size() > kInstalledRowCapacity) {
        return false;
    }
    for (std::size_t row = 0; row < index.size(); ++row) {
        if (index[row].index != row) {
            return false;
        }
    }
    std::size_t saleOffset = 0;
    std::size_t installedOffset = 0;
    for (std::size_t row = 0; row < definitions.size(); ++row) {
        const Definition& definition = definitions[row];
        if (!canonical(definition, index) || definition.saleRowOffset != saleOffset
            || definition.installedRowOffset != installedOffset
            || definition.saleCount > saleRows.size() - saleOffset
            || definition.installedCount > installedRows.size() - installedOffset
            || (row != 0 && definitions[row - 1].index >= definition.index)
            || !canonical_sale_rows(definition, saleRows)
            || !canonical_installed_rows(definition, installedRows)) {
            return false;
        }
        saleOffset += definition.saleCount;
        installedOffset += definition.installedCount;
    }
    return saleOffset == saleRows.size() && installedOffset == installedRows.size();
}

bool replace(std::span<const IndexEntry> index,
             std::span<const Definition> definitions,
             std::span<const SaleRow> saleRows,
             std::span<const InstalledRow> installedRows) noexcept {
    if (!valid(index, definitions, saleRows, installedRows)) {
        return false;
    }
    const std::lock_guard guard(g_lock);
    const bool storedIndex = g_index.replace(index);
    const bool storedDefinitions = g_definitions.replace(definitions);
    const bool storedSaleRows = g_saleRows.replace(saleRows);
    const bool storedInstalledRows = g_installedRows.replace(installedRows);
    return storedIndex && storedDefinitions && storedSaleRows && storedInstalledRows;
}

bool find_hash(std::uint32_t definitionHash, IndexEntry& entry) noexcept {
    entry = {};
    const std::shared_lock guard(g_lock);
    const std::span<const IndexEntry> rows = g_index.rows();
    const auto found =
        std::find_if(rows.begin(), rows.end(), [definitionHash](const IndexEntry& row) {
            return row.definitionHash == definitionHash;
        });
    // a hash carried by two rows names no single vendor, so neither is returned
    const bool unique =
        found != rows.end()
        && std::find_if(found + 1, rows.end(), [definitionHash](const IndexEntry& row) {
               return row.definitionHash == definitionHash;
           }) == rows.end();
    if (unique) {
        entry = *found;
    }
    return unique;
}

bool find_index(std::uint16_t index, IndexEntry& entry) noexcept {
    entry = {};
    const std::shared_lock guard(g_lock);
    const std::span<const IndexEntry> rows = g_index.rows();
    const bool present = index < rows.size();
    if (present) {
        entry = rows[index];
    }
    return present;
}

bool find(std::uint32_t definitionHash, Definition& definition) noexcept {
    definition = {};
    const std::shared_lock guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    const auto found =
        std::find_if(rows.begin(), rows.end(), [definitionHash](const Definition& row) {
            return row.definitionHash == definitionHash;
        });
    const bool present = found != rows.end();
    if (present) {
        definition = *found;
    }
    return present;
}

bool sale_rows(const Definition& definition,
               std::span<SaleRow> output,
               std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return copy_range(
        g_saleRows.rows(), definition.saleRowOffset, definition.saleCount, output, count);
}

bool sale_row(const Definition& definition, std::size_t row, SaleRow& output) noexcept {
    output = {};
    if (row >= definition.saleCount) {
        return false;
    }
    const std::shared_lock guard(g_lock);
    const auto bank = g_saleRows.rows();
    const std::size_t at = static_cast<std::size_t>(definition.saleRowOffset) + row;
    if (at >= bank.size()) {
        return false;
    }
    output = bank[at];
    return true;
}

bool installed_rows(const Definition& definition,
                    std::span<InstalledRow> output,
                    std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return copy_range(g_installedRows.rows(),
                      definition.installedRowOffset,
                      definition.installedCount,
                      output,
                      count);
}

bool installed_row(const Definition& definition, std::size_t row, InstalledRow& output) noexcept {
    output = {};
    if (row >= definition.installedCount) {
        return false;
    }
    const std::shared_lock guard(g_lock);
    const auto bank = g_installedRows.rows();
    const std::size_t at = static_cast<std::size_t>(definition.installedRowOffset) + row;
    if (at >= bank.size()) {
        return false;
    }
    output = bank[at];
    return true;
}

bool snapshot_index(std::span<IndexEntry> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_index.snapshot(output, count);
}

bool snapshot_definitions(std::span<Definition> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.snapshot(output, count);
}

bool snapshot_sale_rows(std::span<SaleRow> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_saleRows.snapshot(output, count);
}

bool snapshot_installed_rows(std::span<InstalledRow> output, std::size_t& count) noexcept {
    const std::shared_lock guard(g_lock);
    return g_installedRows.snapshot(output, count);
}

std::size_t count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_index.count();
}

std::size_t definition_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_definitions.count();
}

std::size_t sale_row_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_saleRows.count();
}

std::size_t installed_row_count() noexcept {
    const std::shared_lock guard(g_lock);
    return g_installedRows.count();
}

} // namespace dawn::state::build_data::vendors
