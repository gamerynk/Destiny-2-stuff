#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace dawn::client::hooks::bootflow {
#if !defined(RESTORATION_NATIVE_PROFILE_SETUP)
namespace {

constexpr std::string_view kUpdateSignatureText =
    "40 53 48 83 EC ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 ? 48 8B D9 E8 ? ? ? ? 84 C0 0F 84 ? "
    "? ? ? 8B 43 38 85 C0 75 ?";
constexpr auto kUpdateSignature =
    signature<signature_length(kUpdateSignatureText)>(kUpdateSignatureText);

struct StepLayout {
    static constexpr std::size_t state = 56;
};

// states with no self-advance: the update posts a setup screen and parks until the UI raises
// the investment event acknowledging it. State 0 posts the first one
constexpr std::array<std::uint32_t, 5> kWaitingStates{0, 1, 3, 5, 7};

// matches no case in the dispatch, so it falls to the last block, which marks the profile
// record and hands off; the same exit the stock game takes once every screen is acknowledged
constexpr std::uint32_t kHandoffState = 9;

constexpr char kStepIncomplete = 0;
using Update = char(__fastcall*)(std::byte*);

hooking::detour::Handle g_handle{};
std::atomic<Update> g_original{nullptr};
std::atomic_bool g_reported{false};

[[nodiscard]] bool is_waiting(std::uint32_t state) noexcept {
    for (const std::uint32_t waiting : kWaitingStates) {
        if (state == waiting) {
            return true;
        }
    }
    return false;
}

// the UI event that acknowledges the setup screens is never raised here, so the step would
// park forever; moving it to the handoff state takes its own exit without posting a screen
__declspec(noinline) char __fastcall update(std::byte* step) noexcept {
    const Update original = g_original.load(std::memory_order_acquire);
    std::uint32_t stateBefore = 0;
    if (step != nullptr) {
        std::memcpy(&stateBefore, step + StepLayout::state, sizeof stateBefore);
        if (is_waiting(stateBefore)) {
            std::memcpy(step + StepLayout::state, &kHandoffState, sizeof kHandoffState);
            if (!g_reported.exchange(true, std::memory_order_relaxed)) {
                core::log::write(core::log::Channel::client,
                                 core::log::Level::info,
                                 "ev=bootflow stage=profile_setup result=skipped");
            }
        }
    }
    return original != nullptr ? original(step) : kStepIncomplete;
}

} // namespace
#endif

bool install_profile_setup_skip() noexcept {
#if defined(RESTORATION_NATIVE_PROFILE_SETUP)
    core::log::write(core::log::Channel::client, core::log::Level::info,
                     "ev=bootflow stage=profile_setup result=native_ui detours=0 variant=1");
    return true;
#else
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kUpdateSignature, "profile_setup_update");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=profile_setup result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&update)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=profile_setup result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<Update>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=profile_setup result=ok");
    return true;
#endif
}

void uninstall_profile_setup_skip() noexcept {
#if !defined(RESTORATION_NATIVE_PROFILE_SETUP)
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_reported.store(false, std::memory_order_release);
#endif
}

} // namespace dawn::client::hooks::bootflow
