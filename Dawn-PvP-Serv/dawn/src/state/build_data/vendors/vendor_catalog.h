#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace dawn::state::build_data::vendors {

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const IndexEntry> index,
                         std::span<const Definition> definitions,
                         std::span<const SaleRow> saleRows,
                         std::span<const InstalledRow> installedRows) noexcept;

[[nodiscard]] bool replace(std::span<const IndexEntry> index,
                           std::span<const Definition> definitions,
                           std::span<const SaleRow> saleRows,
                           std::span<const InstalledRow> installedRows) noexcept;

[[nodiscard]] bool find_hash(std::uint32_t definitionHash, IndexEntry& entry) noexcept;

[[nodiscard]] bool find_index(std::uint16_t index, IndexEntry& entry) noexcept;

[[nodiscard]] bool find(std::uint32_t definitionHash, Definition& definition) noexcept;

[[nodiscard]] bool
sale_rows(const Definition& definition, std::span<SaleRow> output, std::size_t& count) noexcept;

// a purchase names one row; copying the whole range to read it would cost a bank-sized buffer
[[nodiscard]] bool
sale_row(const Definition& definition, std::size_t row, SaleRow& output) noexcept;

[[nodiscard]] bool installed_rows(const Definition& definition,
                                  std::span<InstalledRow> output,
                                  std::size_t& count) noexcept;

[[nodiscard]] bool
installed_row(const Definition& definition, std::size_t row, InstalledRow& output) noexcept;

[[nodiscard]] bool snapshot_index(std::span<IndexEntry> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot_definitions(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot_sale_rows(std::span<SaleRow> output, std::size_t& count) noexcept;

[[nodiscard]] bool snapshot_installed_rows(std::span<InstalledRow> output,
                                           std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

[[nodiscard]] std::size_t definition_count() noexcept;

[[nodiscard]] std::size_t sale_row_count() noexcept;

[[nodiscard]] std::size_t installed_row_count() noexcept;

} // namespace dawn::state::build_data::vendors
