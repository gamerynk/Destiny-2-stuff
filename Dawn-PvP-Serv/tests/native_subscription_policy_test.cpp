#include <Windows.h>
#include <cstdio>
#include <string_view>
#include "../dawn/src/core/logging/log.h"
#include "../dawn/src/client/hooks/queuez/queuez_hook_lifecycle.cpp"
namespace fixture { bool physical{}; int installs{}, removals{}, reports{}; bool allowed{true}; }
namespace dawn::core::log {
void write(Channel, Level, std::string_view line) noexcept {
    if (line.find("active_detours=0") != std::string_view::npos) ++fixture::reports;
}
}
namespace dawn::client::hooks::queuez {
bool install_null_payload_guard() noexcept { ++fixture::installs; fixture::physical=fixture::allowed; return fixture::physical; }
void uninstall_null_payload_guard() noexcept { ++fixture::removals; fixture::physical=false; }
bool null_payload_guard_installed() noexcept { return fixture::physical; }
}
int main(int argc,char** argv) {
    if (argc!=3) return 1;
    const bool native=true;
    if (argv[1][0]=='-') SetEnvironmentVariableW(L"DAWN_NATIVE_SUBSCRIPTION_RESPONSES",nullptr);
    else SetEnvironmentVariableW(L"DAWN_NATIVE_SUBSCRIPTION_RESPONSES",argv[1][0]=='1'?L"1":L"0");
    namespace q=dawn::client::hooks::queuez;
    if (q::native_subscription_requested()!=native || q::is_installed()) return 2;
    if (!q::install() || !q::is_installed() || !q::install()) return 3;
    if (q::null_payload_guard_installed()==native || fixture::installs!=(native?0:2) || fixture::reports!=(native?1:0)) return 4;
    SetEnvironmentVariableW(L"DAWN_NATIVE_SUBSCRIPTION_RESPONSES",native?L"0":L"1");
    if (q::native_subscription_requested()!=native) return 5;
    q::uninstall();
    if (q::is_installed() || q::null_payload_guard_installed() || fixture::removals!=0) return 6;
    fixture::allowed=false;
    if (q::install()!=native || q::is_installed()!=native) return 7;
    q::uninstall();
    std::puts("PASS: QueueZ native policy, zero detour installs/removals under all legacy flags, readiness/cleanup");
}
