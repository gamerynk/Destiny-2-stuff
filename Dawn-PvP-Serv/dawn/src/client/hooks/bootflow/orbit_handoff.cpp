#include <array>
#include <atomic>
#include <cstddef>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace dawn::client::hooks::bootflow {
#if !defined(RESTORATION_NATIVE_ORBIT_HANDOFF)
namespace {

// destination-hold predicate of the orbit setup step; its prologue repeats across the image,
// so the pattern runs on through the call and flag test. Displacements are wildcarded
constexpr std::string_view kHoldSignatureText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B D9 E8 ? ? ? ? 80 3D ? ? ? ? 00 48 8B F8 75 ?";
constexpr auto kHoldSignature = signature<signature_length(kHoldSignatureText)>(kHoldSignatureText);

// the native predicate holds for an armed pending destination or a cinematic under a 5,000 ms
// timer; this answer skips the wait. Single call site (the step's update), so nothing else sees it
constexpr bool kReleased = false;

hooking::detour::Handle g_handle{};
std::atomic_bool g_reported{false};

__declspec(noinline) bool __fastcall destination_hold(void* stepCtx) noexcept {
    (void)stepCtx;
    if (!g_reported.exchange(true, std::memory_order_relaxed)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=bootflow stage=orbit_handoff result=released");
    }
    return kReleased;
}

} // namespace
#endif

bool install_orbit_handoff() noexcept {
#if defined(RESTORATION_NATIVE_ORBIT_HANDOFF)
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=orbit_handoff result=native detours=0 variant=1");
    return true;
#else
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kHoldSignature, "orbit_destination_hold");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=orbit_handoff result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&destination_hold)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=orbit_handoff result=fail reason=attach");
        return false;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=orbit_handoff result=ok");
    return true;
#endif
}

void uninstall_orbit_handoff() noexcept {
#if !defined(RESTORATION_NATIVE_ORBIT_HANDOFF)
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_reported.store(false, std::memory_order_release);
#endif
}

} // namespace dawn::client::hooks::bootflow
