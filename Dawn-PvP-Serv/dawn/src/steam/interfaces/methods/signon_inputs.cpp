#include "../../runtime/transport/steam_transport_bridge.h"
#include "../internal.h"

namespace dawn::steam::interfaces::methods {

const char* country([[maybe_unused]] void* self) noexcept {
    return runtime::transport::country();
}

std::uint32_t server_real_time([[maybe_unused]] void* self) noexcept {
    return runtime::transport::server_real_time();
}

DWORD get_app_id([[maybe_unused]] void* self) noexcept {
    return app_id();
}

} // namespace dawn::steam::interfaces::methods
