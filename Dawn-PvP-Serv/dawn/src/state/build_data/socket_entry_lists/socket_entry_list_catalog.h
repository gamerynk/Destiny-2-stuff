#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "definition.h"

namespace dawn::state::build_data::socket_entry_lists {

void clear() noexcept;

[[nodiscard]] bool valid(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool replace(std::span<const Definition> definitions) noexcept;

[[nodiscard]] bool find(std::uint16_t definitionIndex, Definition& definition) noexcept;

[[nodiscard]] bool snapshot(std::span<Definition> output, std::size_t& count) noexcept;

[[nodiscard]] std::size_t count() noexcept;

[[nodiscard]] bool valid_entry_tables(std::span<const EntryTable> tables) noexcept;

[[nodiscard]] bool replace_entry_tables(std::span<const EntryTable> tables) noexcept;

[[nodiscard]] bool find_entry_table(std::uint16_t definitionIndex, EntryTable& table) noexcept;

[[nodiscard]] bool snapshot_entry_tables(std::span<EntryTable> output, std::size_t& count) noexcept;

} // namespace dawn::state::build_data::socket_entry_lists
