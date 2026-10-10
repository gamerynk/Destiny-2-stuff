#include "../../../../targets/game.h"
#include "../internal.h"
#include "../runtime.h"
namespace dawn::client::hooks::network::content_config {
bool install() noexcept {
    AcquireSRWLockExclusive(&g_lifecycleLock);
    const auto address = targets::game::network::get().contentManifestSignatureGate;
    const bool installed = g_accepting ? gate::reassert(g_gateOwnership)
        : g_gateOwnership.address == nullptr && native_work::resolve(address, g_nativeWork)
          && gate::disable(address, g_gateOwnership);
    if (!installed && g_gateOwnership.address == nullptr) g_nativeWork = {};
    g_accepting = installed;
    ReleaseSRWLockExclusive(&g_lifecycleLock);
    return installed;
}
}
