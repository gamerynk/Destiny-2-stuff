#include "server_runtime.h"

#include "../bap/runtime.h"
#include "../bap/encrypted/push/snapshot/snapshot.h"
#include "../discovery/discovery_runtime.h"
#include "../gameplay/gameplay_runtime.h"
#include "../http/transport/http_listener.h"
#include "../transport/bap_listener.h"
#include "../ui/runtime/server_ui_module_runtime.h"

#if defined(RESTORATION_DEDICATED_SERVER)
#include "../../../../RestorationServer/src/realtime_store.h"
#endif

namespace dawn::server {

bool initialize_dedicated() noexcept {
    if (!bap::encrypted::push::snapshot::preflight_required_families()) {
        return false;
    }
    if (!discovery::initialize()) {
        return false;
    }
    if (!http::transport::initialize()) {
        discovery::shutdown();
        return false;
    }
    if (!transport::initialize()) {
        http::transport::shutdown();
        discovery::shutdown();
        return false;
    }
    if (!gameplay::initialize()) {
        transport::shutdown();
        http::transport::shutdown();
        discovery::shutdown();
        return false;
    }
#if defined(RESTORATION_DEDICATED_SERVER)
    // Family 7 must keep the native group-zero owner's exact transport identity; advertising this
    // process's gameplay UDP endpoint would create a second native fireteam host
    bap::encrypted::push::snapshot::set_join_descriptor_provider(
        &::restoration::realtime_store::native_fireteam_descriptor);
#endif
    return true;
}

void service(std::uint64_t now) noexcept {
    discovery::service();
    http::transport::service();
    transport::service(now);
    gameplay::service(now);
}

void shutdown() noexcept {
    ui::runtime::shutdown();
#if defined(RESTORATION_DEDICATED_SERVER)
    bap::encrypted::push::snapshot::set_join_descriptor_provider(nullptr);
#endif
    gameplay::shutdown();
    transport::shutdown();
    http::transport::shutdown();
    discovery::shutdown();
    bap::shutdown();
}

} // namespace dawn::server
