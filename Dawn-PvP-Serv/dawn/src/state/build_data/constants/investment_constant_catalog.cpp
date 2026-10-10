#include "investment_constant_catalog.h"

#include "../table.h"

namespace dawn::state::build_data::constants {
namespace {

// one row, not a table, so the value sits directly under the shared Lock
Lock g_lock;
InvestmentConstants g_constants{};

} // namespace

void clear() noexcept {
    const Lock::Exclusive guard(g_lock);
    g_constants = {};
}

bool replace(const InvestmentConstants& value) noexcept {
    if (!valid(value)) {
        return false;
    }
    const Lock::Exclusive guard(g_lock);
    g_constants = value;
    return true;
}

bool find(InvestmentConstants& value) noexcept {
    const Lock::Shared guard(g_lock);
    value = g_constants;
    return valid(value);
}

InvestmentConstants snapshot() noexcept {
    InvestmentConstants value{};
    (void)find(value);
    return value;
}

} // namespace dawn::state::build_data::constants
