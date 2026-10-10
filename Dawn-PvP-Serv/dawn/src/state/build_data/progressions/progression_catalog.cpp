#include "progression_catalog.h"

#include "../table.h"

namespace dawn::state::build_data::progressions {
namespace {

Lock g_lock;
Table<Definition, kDefinitionCapacity> g_definitions;

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_definitions.clear();
}

bool valid(std::span<const Definition> definitions) noexcept {
    if (definitions.empty() || definitions.size() > kDefinitionCapacity) {
        return false;
    }
    for (std::size_t row = 0; row < definitions.size(); ++row) {
        if (definitions[row].definitionIndex != row) {
            return false;
        }
    }
    return true;
}

bool replace(std::span<const Definition> definitions) noexcept {
    if (!valid(definitions)) {
        return false;
    }
    const Lock::Exclusive guard(g_lock);
    return g_definitions.replace(definitions);
}

bool slots(Scope scope, std::span<std::uint16_t> output, std::size_t& count) noexcept {
    count = 0;
    const Lock::Shared guard(g_lock);
    const std::span<const Definition> rows = g_definitions.rows();
    bool complete = !rows.empty();
    for (const Definition& row : rows) {
        if (!complete) {
            break;
        }
        if (row.scope != scope) {
            continue;
        }
        // a scope with more definitions than its array has slots would silently lose the tail
        complete = count < output.size();
        if (complete) {
            output[count++] = row.definitionIndex;
        }
    }
    if (!complete) {
        count = 0;
    }
    return complete;
}

bool snapshot(std::span<Definition> output, std::size_t& count) noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.snapshot(output, count);
}

std::size_t count() noexcept {
    const Lock::Shared guard(g_lock);
    return g_definitions.count();
}

} // namespace dawn::state::build_data::progressions
