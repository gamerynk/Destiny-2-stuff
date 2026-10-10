#include <cassert>
#include <string>
#include "../dawn/src/client/hooking/detour.cpp"
namespace fixture {
bool frozen{};
int phase{};
std::string event;
}
namespace dawn::core::log {
void write(Channel, Level, std::string_view event) noexcept {
    assert(!fixture::frozen);
    fixture::event = event;
}
}
namespace dawn::client::hooking::detour::transaction {
Failure last_failure() noexcept {
    return fixture::phase == 1 ? Failure{"open_thread", ERROR_ACCESS_DENIED, 67, 123}
         : fixture::phase == 3 ? Failure{"transaction_commit", ERROR_INVALID_DATA, 68, 0}
                               : Failure{"none", NO_ERROR, 68, 0};
}
bool begin(Threads&) noexcept { fixture::frozen = fixture::phase != 1; return fixture::phase != 1; }
bool abort(Threads&) noexcept { fixture::frozen = false; return true; }
bool commit(Threads&) noexcept { fixture::frozen = false; return fixture::phase != 3; }
}
extern "C" LONG WINAPI DetourAttach(PVOID*, PVOID) {
    assert(fixture::frozen);
    return fixture::phase == 2 ? ERROR_INVALID_BLOCK : NO_ERROR;
}
extern "C" LONG WINAPI DetourDetach(PVOID*, PVOID) { return NO_ERROR; }
namespace dawn::client::hooking::detour {
UninstallResult uninstall(std::span<Handle>, std::span<const ProtectedCodeEntry>) noexcept {
    return UninstallResult::failed;
}
}
int main() {
    using namespace dawn::client::hooking::detour;
    const Spec spec{reinterpret_cast<void*>(1), reinterpret_cast<void*>(2)};
    Handle output{};
    for (int phase = 1; phase <= 3; ++phase) {
        fixture::phase = phase;
        assert(!install(spec, output) && !output.attached && output.original == nullptr);
        assert(fixture::event.find("ev=detour_install result=fail") != std::string::npos);
        assert(fixture::event.find(phase == 1 ? "stage=open_thread error=5"
             : phase == 2 ? "stage=attach error=9 hook_index=0"
                          : "stage=transaction_commit error=13") != std::string::npos);
    }
    fixture::phase = 0;
    fixture::event.clear();
    assert(install(spec, output) && output.attached && output.original == spec.target);
    assert(fixture::event.empty());
    std::puts("PASS: attach failure stage/error logged only after freeze release; atomic outputs preserved");
}
