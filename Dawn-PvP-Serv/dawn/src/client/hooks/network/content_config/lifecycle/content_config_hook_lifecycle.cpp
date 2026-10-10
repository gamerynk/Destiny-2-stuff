#include "../internal.h"
#include "../runtime.h"
namespace dawn::client::hooks::network::content_config {
bool prepare_native_request() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    const bool prepared = g_accepting && gate::reassert(g_gateOwnership);
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    return prepared;
}
bool is_installed() noexcept {
    AcquireSRWLockShared(&g_lifecycleLock);
    const bool installed = g_accepting && g_gateOwnership.address != nullptr;
    ReleaseSRWLockShared(&g_lifecycleLock);
    return installed;
}
bool has_ownership() noexcept {
    AcquireSRWLockShared(&g_lifecycleLock);
    const bool owned = g_gateOwnership.address != nullptr;
    ReleaseSRWLockShared(&g_lifecycleLock);
    return owned;
}
}
