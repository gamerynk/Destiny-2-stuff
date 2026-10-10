#include "socket_entry_list_catalog.h"

#include <array>

#include "../table.h"

namespace dawn::state::build_data::socket_entry_lists {
namespace {

// one lock covers both tables; an entry table only means something against its own list rows
Lock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;
Table<EntryTable, kEntryTableCapacity> g_entryTables;

[[nodiscard]] bool ready_mask_fits(const Definition& definition) noexcept {
    if (definition.entryCount > kEntryCapacity) {
        return false;
    }
    const std::uint64_t allowedMask =
        definition.entryCount == 0
            ? 0
            : (std::uint64_t{1} << static_cast<unsigned>(definition.entryCount)) - 1U;
    return (definition.readyMask & ~allowedMask) == 0;
}

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_definitions.clear();
    g_entryTables.clear();
}

bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }

    std::array<bool, kDefinitionCapacity> occupied{};
    for (const Definition& definition : definitions) {
        if (definition.definitionIndex >= definitions.size() || occupied[definition.definitionIndex]
            || !ready_mask_fits(definition)) {
            return false;
        }
        occupied[definition.definitionIndex] = true;
    }
    return true;
}

bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }

    const Lock::Exclusive guard(g_lock);
    const std::span<Definition> storage = g_definitions.reset(definitions.size());
    if (storage.size() != definitions.size()) {
        return false;
    }
    for (const Definition& definition : definitions) {
        storage[definition.definitionIndex] = definition;
    }
    return true;
}

bool find(std::uint16_t definitionIndex, Definition& definition) noexcept {
    definition = {};
    const Lock::Shared guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    const bool found = definitionIndex < rows.size();
    if (found) {
        definition = rows[definitionIndex];
    }
    return found;
}

bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.snapshot(output, count);
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.count();
}

bool valid_entry_tables(std::span<const EntryTable> tables) noexcept {
    if (tables.size() > kEntryTableCapacity) {
        return false;
    }
    for (std::size_t index = 0; index < tables.size(); ++index) {
        if (tables[index].definitionIndex == kNoEntryTable) {
            return false;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (tables[prior].definitionIndex == tables[index].definitionIndex) {
                return false;
            }
        }
    }
    return true;
}

bool replace_entry_tables(std::span<const EntryTable> tables) noexcept {
    if (!valid_entry_tables(tables)) {
        return false;
    }
    const Lock::Exclusive guard(g_lock);
    return g_entryTables.replace(tables);
}

bool find_entry_table(std::uint16_t definitionIndex, EntryTable& table) noexcept {
    table = {};
    const Lock::Shared guard(g_lock);
    for (const EntryTable& row : g_entryTables.rows()) {
        if (row.definitionIndex == definitionIndex) {
            table = row;
            return true;
        }
    }
    return false;
}

bool snapshot_entry_tables(std::span<EntryTable> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_entryTables.snapshot(output, count);
}

} // namespace dawn::state::build_data::socket_entry_lists
