#include "entitlement_runtime.h"

#include "validation.h"

namespace dawn::state::entitlements {
namespace {

[[nodiscard]] Table& storage() noexcept {
    static Table table = authored();
    return table;
}

} // namespace

bool publish(const Table& table) noexcept {
    if (!valid(table)) {
        return false;
    }
    storage() = table;
    return true;
}

const Table& get() noexcept {
    return storage();
}

void clear() noexcept {
    storage() = authored();
}

} // namespace dawn::state::entitlements
