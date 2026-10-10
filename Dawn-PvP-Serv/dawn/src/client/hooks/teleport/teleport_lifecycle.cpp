#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../player/player_position.h"
#include "../bootflow/bootflow_hook_lifecycle.h"
#include "../bootflow/native_fireteam_callback.h"
#include "internal.h"
#include "runtime.h"

namespace dawn::client::hooks::teleport {
namespace {

constexpr std::string_view kControlledHandleText =
    "40 53 48 83 EC 20 48 8B D9 C7 01 FF FF FF FF 48 8D 4C 24 30 E8 ? ? ? ? 8B 44 24 30 "
    "83 F8 FF 74 18 25 FF 1F 00 00 0F AF 05";
constexpr auto kControlledHandlePattern =
    signature<signature_length(kControlledHandleText)>(kControlledHandleText);

constexpr std::string_view kNativePlayerPhysicsText =
    "48 89 5C 24 08 57 48 83 EC 20 C7 01 FF FF FF FF 8B DA 48 8B F9 "
    "83 FA FF 0F 84 7D 00 00 00 66 90 8B CB E8 ? ? ? ? 8B 00 89 07 "
    "83 F8 FF 74 47";
constexpr auto kNativePlayerPhysics =
    signature<signature_length(kNativePlayerPhysicsText)>(kNativePlayerPhysicsText);

[[nodiscard]] NativePlayerBodyLookup native_player_lookup() noexcept {
    std::byte* const getter = scan_main_image_unique(kNativePlayerPhysics, "native_player_physics");
    if (!getter) return {};
    // the pool and native unit array reads in the same getter; decoding their RIP operands
    // avoids guessed globals
    std::array<unsigned char, 3> pool{}, stride{}, units{};
    SIZE_T copied{};
    const auto prefix = [&](std::size_t offset, std::array<unsigned char, 3>& output) {
        return ReadProcessMemory(GetCurrentProcess(), getter + offset, output.data(),
                                 output.size(), &copied) && copied == output.size();
    };
    if (!prefix(0x4D, pool) || pool != std::array<unsigned char, 3>{0x48, 0x8B, 0x0D}
        || !prefix(0x83, stride) || stride != std::array<unsigned char, 3>{0x0F, 0xAF, 0x1D}
        || !prefix(0x8C, units) || units != std::array<unsigned char, 3>{0x48, 0x03, 0x05}) return {};
    const std::byte* const poolPointer = resolve_relative(getter + 0x50, getter + 0x54);
    const std::byte* const unitStride = resolve_relative(getter + 0x86, getter + 0x8A);
    const std::byte* const unitsPointer = resolve_relative(getter + 0x8F, getter + 0x93);
    return {reinterpret_cast<NativePhysicsHandle>(getter), poolPointer, unitsPointer,
            unitStride, unitStride + sizeof(std::uint32_t)};
}

std::atomic_bool g_installed{false};
} // namespace

bool install() noexcept {
    if(g_installed.load(std::memory_order_acquire)) return true;
    bool nativeObservers=false;
#if defined(RESTORATION_NATIVE_CAMERA_OBSERVERS) && RESTORATION_NATIVE_CAMERA_OBSERVERS
    nativeObservers=bootflow::native_camera_observers_available();
#endif
    if(!nativeObservers) {
        core::log::write(core::log::Channel::client,core::log::Level::info,
            "ev=player_observer stage=install result=unavailable hooks=0 reason=no_native_frame");
    }
    std::byte* const handle=scan_main_image_unique(kControlledHandlePattern,"teleport_handle");
    if(!handle) {
        core::log::write(core::log::Channel::client,core::log::Level::warn,
            "ev=player_observer stage=install result=unavailable hooks=0 reason=no_controlled_handle");
        g_installed.store(true,std::memory_order_release);
        return true;
    }
    publish_targets(reinterpret_cast<ControlledHandle>(handle));
    const NativePlayerBodyLookup lookup=native_player_lookup();
    publish_native_player_lookup(lookup);
    core::log::write(core::log::Channel::client,core::log::Level::info,lookup.getter
        ? "ev=player_observer stage=install result=ready hooks=0 camera=native_frame body=native"
        : "ev=player_observer stage=install result=unavailable hooks=0 reason=no_native_body");
    g_installed.store(true,std::memory_order_release);
    return true;
}

bool uninstall() noexcept {
    if(!g_installed.load(std::memory_order_acquire)) return true;
    g_installed.store(false,std::memory_order_release);
    clear_targets();
    client::player::position::reset();
    return true;
}
} // namespace dawn::client::hooks::teleport
