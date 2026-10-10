#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#define RESTORATION_NATIVE_CAMERA_OBSERVERS 1
#include "../dawn/src/client/hooks/teleport/teleport_lifecycle.cpp"
namespace fixture {
unsigned checks{}, scans{}, published{}, cleared{};
bool nativeReady{}, missingHandle{};
std::array<std::byte, 256> handle{};
void require(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr, "%s\n", why); std::exit(1); } }
}
namespace dawn::core::log { void write(Channel, Level, std::string_view) noexcept {} }
namespace dawn::client::patterns {
std::byte* scan_main_image_unique(std::span<const PatternByte>, const char* label) noexcept {
    ++fixture::scans;
    return std::string_view(label)=="teleport_handle" && !fixture::missingHandle ? fixture::handle.data() : nullptr;
}
std::byte* resolve_relative(const std::byte*, const std::byte*) noexcept { return nullptr; }
}
namespace dawn::client::hooks::bootflow {
bool native_camera_observers_available() noexcept { return fixture::nativeReady; }
}
namespace dawn::client::player::position { void reset() noexcept {} }
namespace dawn::client::hooks::teleport {
void publish_targets(ControlledHandle) noexcept { ++fixture::published; }
void publish_native_player_lookup(NativePlayerBodyLookup) noexcept {}
void clear_targets() noexcept { ++fixture::cleared; }
}
int main() {
    namespace t = dawn::client::hooks::teleport;
    for (bool available : {true, false}) {
        fixture::nativeReady = available;
        const auto scans = fixture::scans;
        fixture::require(t::install(), "optional native observer must not block startup");
        const auto installedScans = fixture::scans;
        fixture::require(t::install() && fixture::scans == installedScans, "install is idempotent");
        fixture::require(installedScans == scans + 2, "only controlled handle and native body lookup are scanned");
        fixture::require(t::uninstall() && !t::g_installed.load(), "native observer releases helpers");
    }
    fixture::missingHandle = true;
    const auto published = fixture::published;
    fixture::require(t::install() && fixture::published == published, "missing optional target must not publish invalid helpers");
    fixture::require(t::uninstall(), "unavailable observer shuts down cleanly");
    std::printf("PASS %u native observer checks; linked without detour implementation\n", fixture::checks);
}
