#include "runtime.h"

#include "../../core/identity/device_identity.h"
#include "transport/steam_transport_bridge.h"

namespace dawn::steam {

bool initialize_local_identity(void* module) noexcept {
#if defined(RESTORATION_UNIVERSAL_CLIENT) && RESTORATION_UNIVERSAL_CLIENT
    if (!runtime::transport::initialize(app_id())) {
        return false;
    }
    if (!core::identity::device_identity::initialize(
            module, runtime::transport::platform_id())) {
        runtime::transport::shutdown();
        return false;
    }
    return true;
#else
    return module != nullptr;
#endif
}

std::uint64_t local_steam_id() noexcept {
#if defined(RESTORATION_UNIVERSAL_CLIENT) && RESTORATION_UNIVERSAL_CLIENT
    return core::identity::device_identity::platform_id();
#else
    return kBuildLocalSteamId;
#endif
}

void shutdown_local_identity() noexcept {
#if defined(RESTORATION_UNIVERSAL_CLIENT) && RESTORATION_UNIVERSAL_CLIENT
    core::identity::device_identity::shutdown();
    runtime::transport::shutdown();
#endif
}

} // namespace dawn::steam
