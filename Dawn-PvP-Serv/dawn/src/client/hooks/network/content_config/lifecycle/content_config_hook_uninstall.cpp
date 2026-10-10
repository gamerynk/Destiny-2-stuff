#include "../internal.h"
#include "../runtime.h"
namespace dawn::client::hooks::network::content_config {
bool uninstall() noexcept {
    // curl must detach first; the exclusive lock serializes the final native URL gate check
    AcquireSRWLockExclusive(&g_lifecycleLock);
    g_accepting = false;
    const bool restored = g_gateOwnership.address == nullptr
        || native_work::restore_when_idle(g_nativeWork, g_gateOwnership);
    if (restored) g_nativeWork = {};
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    return restored;
}
}
