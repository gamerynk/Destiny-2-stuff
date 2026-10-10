#include "hash_name_catalog.h"

#include <algorithm>

#include "../table.h"

namespace dawn::state::build_data::hash_names {
namespace {

Lock g_lock;
Table<Name, kNameCapacity> g_names;

[[nodiscard]] bool canonical(const Name& row) noexcept {
    if (row.nameLength == 0 || row.nameLength > kNameLength) {
        return false;
    }
    for (std::size_t index = 0; index < kNameLength; ++index) {
        const bool declared = index < row.nameLength;
        // storage past the name must stay zero, or two caches of the same packages could differ byte for byte
        if (declared ? !name_character(row.name[index]) : row.name[index] != '\0') {
            return false;
        }
    }
    return true;
}

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_names.clear();
}

bool valid(std::span<const Name> names) noexcept {
    if (names.size() > kNameCapacity) {
        return false;
    }
    for (std::size_t row = 0; row < names.size(); ++row) {
        // a repeated or unordered hash would make lookup depend on row order
        if (!canonical(names[row]) || (row != 0 && names[row - 1].hash >= names[row].hash)) {
            return false;
        }
    }
    return true;
}

bool replace(std::span<const Name> names) noexcept {
    if (!valid(names)) {
        return false;
    }
    const Lock::Exclusive guard(g_lock);
    return g_names.replace(names);
}

bool find(std::uint32_t hash, Name& name) noexcept {
    name = {};
    const Lock::Shared guard(g_lock);
    const std::span<const Name> rows = g_names.rows();
    const auto found =
        std::lower_bound(rows.begin(), rows.end(), hash, [](const Name& row, std::uint32_t key) {
            return row.hash < key;
        });
    const bool present = found != rows.end() && found->hash == hash;
    if (present) {
        name = *found;
    }
    return present;
}

bool snapshot(std::span<Name> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_names.snapshot(output, count);
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_names.count();
}

} // namespace dawn::state::build_data::hash_names
