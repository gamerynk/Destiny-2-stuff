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
#if !defined(RESTORATION_NATIVE_CHARACTER_SELECTION)
namespace {

// enter handler of the character sign-in step, anchored on its two setup stores so it cannot
// match a sibling step's handler
constexpr std::string_view kEnterSignatureText =
    "40 53 48 83 EC ? 48 8B D9 C7 41 38 FF FF FF FF 66 C7 41 3C 00 00 33 D2";
constexpr auto kEnterSignature =
    signature<signature_length(kEnterSignatureText)>(kEnterSignatureText);

struct StepLayout {
    // the exit gate needs this non-zero to stay on select; the enter handler clears it, so the
    // step would skip selection on its first frame
    static constexpr std::size_t stayFlag = 72;
};

constexpr std::uint8_t kHold = 1;
using EnterHandler = void(__fastcall*)(std::byte*);

hooking::detour::Handle g_handle{};
std::atomic<EnterHandler> g_original{nullptr};
std::atomic_bool g_reported{false};

// runs after the original writes its defaults, so the original cannot overwrite this write
__declspec(noinline) void __fastcall enter_handler(std::byte* step) noexcept {
    const EnterHandler original = g_original.load(std::memory_order_acquire);
    if (original != nullptr) {
        original(step);
    }
    if (step == nullptr) {
        return;
    }
    // never treat a retained solo-owner descriptor as a join authorization; explicit joins use the TARGET path
    (void)prearm_dedicated_fireteam_bootstrap();
    std::memcpy(step + StepLayout::stayFlag, &kHold, sizeof kHold);
    if (!g_reported.exchange(true, std::memory_order_relaxed)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=bootflow stage=character_select result=held");
    }
}

} // namespace
#endif

bool install_character_select_hold() noexcept {
#if defined(RESTORATION_NATIVE_CHARACTER_SELECTION)
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=character_select result=native_ui detours=0 variant=1");
    return true;
#else
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kEnterSignature, "character_signin_enter");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=character_select result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&enter_handler)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=character_select result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<EnterHandler>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=character_select result=ok");
    return true;
#endif
}

void uninstall_character_select_hold() noexcept {
#if !defined(RESTORATION_NATIVE_CHARACTER_SELECTION)
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_reported.store(false, std::memory_order_release);
#endif
}

} // namespace dawn::client::hooks::bootflow
