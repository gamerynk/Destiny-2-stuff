#include <Windows.h>

#include <array>

#include "../../core/logging/log.h"
#include "../../core/settings/settings.h"
#include "../hooks/egress/runtime.h"
#include "../network/native_routes/contract.h"
#include "../network/native_routes/runtime.h"
#include "../../steam/runtime/native_network_diagnostics.h"
#include "network_route_policy.h"

namespace dawn::client::runtime::network_route_policy {
namespace {
SRWLOCK g_lock{SRWLOCK_INIT};
enum class Publication { pending, ready, failed };
Publication g_publication{};
std::array<unsigned char, 4> g_address{};
bool g_reported{};
}

bool native_requested() noexcept {
#if defined(RESTORATION_NATIVE_NETWORK_ROUTES) && RESTORATION_NATIVE_NETWORK_ROUTES
    return true;
#else
    static const bool selected = []() noexcept {
        wchar_t value[2]{};
        return GetEnvironmentVariableW(L"DAWN_NATIVE_NETWORK_ROUTES", value, 2) == 1
            && value[0] == L'1';
    }();
    return selected;
#endif
}

bool prepare() noexcept {
    return native_requested() || hooks::egress::install();
}

bool initialize() noexcept {
    if (!native_requested()) return hooks::egress::is_installed();
    AcquireSRWLockExclusive(&g_lock);
    const auto& settings = core::settings::get();
    const bool contract = network::native_routes::valid_contract(settings);
    if (g_publication == Publication::pending) {
        const bool published = contract && network::native_routes::publish_discovery(
            GetModuleHandleW(nullptr), settings.client.externalServer);
        g_publication = published ? Publication::ready : Publication::failed;
        if (published) g_address = settings.client.externalServer.address;
    }
    // discovery storage belongs to the loaded game image; a second Core init can reuse it only
    // with the same destination, changing it requires a restart
    const bool result = contract && g_publication == Publication::ready
        && g_address == settings.client.externalServer.address;
    if (!result) g_publication = Publication::failed;
    ReleaseSRWLockExclusive(&g_lock);
    if (!result) core::log::write(core::log::Channel::client, core::log::Level::error,
        "ev=native_routes stage=publication result=fail restart_required=1");
    if (result) steam::runtime::native_network_diagnostics::start();
    return result;
}

bool ready() noexcept {
    if (!native_requested()) return hooks::egress::is_installed();
    AcquireSRWLockShared(&g_lock);
    const bool result = g_publication == Publication::ready;
    ReleaseSRWLockShared(&g_lock);
    return result;
}

void report() noexcept {
    if (!native_requested()) {
        hooks::egress::report_installation();
        return;
    }
    AcquireSRWLockExclusive(&g_lock);
    const bool reportNow = !g_reported && g_publication == Publication::ready;
    g_reported |= reportNow;
    ReleaseSRWLockExclusive(&g_lock);
    if (reportNow) core::log::write(core::log::Channel::client, core::log::Level::info,
        "ev=native_routes stage=publication result=ok discovery_hosts=20 windows_detours=0 live_test=1");
}
}
