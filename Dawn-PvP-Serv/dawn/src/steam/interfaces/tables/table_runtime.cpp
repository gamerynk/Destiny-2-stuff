#include "internal.h"

namespace dawn::steam::interfaces::tables {
namespace {

INIT_ONCE g_interfaceInit{INIT_ONCE_STATIC_INIT};

BOOL CALLBACK initialize_tables(PINIT_ONCE, PVOID, PVOID*) noexcept {
    initialize_common();
    initialize_networking();
    return TRUE;
}

} // namespace

bool initialize() noexcept {
    return InitOnceExecuteOnce(&g_interfaceInit, initialize_tables, nullptr, nullptr) != FALSE;
}

} // namespace dawn::steam::interfaces::tables
