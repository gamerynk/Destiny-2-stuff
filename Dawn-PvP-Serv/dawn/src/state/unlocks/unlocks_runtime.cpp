#include "unlocks_runtime.h"

#include <Windows.h>

namespace dawn::state::unlocks {
namespace {

Table g_table{};
SRWLOCK g_lock{SRWLOCK_INIT};

} // namespace

void publish(const Table& table) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_table = table;
    ReleaseSRWLockExclusive(&g_lock);
}

const Table& get() noexcept {
    return g_table;
}

void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_table = Table{};
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::state::unlocks
