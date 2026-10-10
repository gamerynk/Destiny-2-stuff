#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "internal.h"
#if defined(RESTORATION_NATIVE_ORBIT_SLICE_SET) && RESTORATION_NATIVE_ORBIT_SLICE_SET
#include "native_orbit_asset_policy.h"
#endif

namespace dawn::client::hooks::bootflow {
namespace {

// slice-set name-hash picker, anchored on the unset-sentinel store to its output and the read
// of the loader context's target field; together they are unique
constexpr std::string_view kPickerSignatureText = "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA C7 02 C5 "
                                                  "9D 1C 81 48 8B F9 E8 ? ? ? ? 8B 97 04 01 00 00";
constexpr auto kPickerSignature =
    signature<signature_length(kPickerSignatureText)>(kPickerSignatureText);

// given for the one target the picker cannot find; otherwise it asks for a hash this build
// lacks, the map load fails and the world is torn down
constexpr std::uint32_t kOrbitSliceSetHash = 0x8265820B;
constexpr std::uint32_t kOrbitTargetSelector = 0;
constexpr std::uint32_t kUnresolvedNameHash = 0x811C9DC5;
constexpr std::size_t kLoaderContextPrefixSize = 0x104;

struct LoaderContext {
    std::array<std::byte, kLoaderContextPrefixSize> opaque00{};
    std::uint32_t targetSelector{};
};

static_assert(offsetof(LoaderContext, targetSelector) == kLoaderContextPrefixSize);

using PickTarget = std::uint32_t*(__fastcall*)(LoaderContext*, std::uint32_t*) noexcept;

hooking::detour::Handle g_handle{};

// the original finds every other target and writes the unset sentinel first, so a surviving
// sentinel means the registry lookup missed
std::uint32_t* __fastcall pick_target(LoaderContext* context, std::uint32_t* selected) noexcept {
    const auto original = reinterpret_cast<PickTarget>(g_handle.original);
    std::uint32_t* const result = original(context, selected);
    if (context != nullptr && selected != nullptr && context->targetSelector == kOrbitTargetSelector
        && *selected == kUnresolvedNameHash) {
        *selected = kOrbitSliceSetHash;
    }
    return result;
}

} // namespace

bool install_orbit_slice_set() noexcept {
#if defined(RESTORATION_NATIVE_PACKAGE_TRUST) && RESTORATION_NATIVE_PACKAGE_TRUST
    // signed stock content has no selector0 alias; native B47794 completes the loader at phase 5
    // (B478CD), which downstream accepts. Keep that path, no alias or picker fallback
    core::log::write(core::log::Channel::client,core::log::Level::info,
        "ev=bootflow stage=slice_set result=native_stock detours=0 missing_target=native_loader");
    return !g_handle.attached;
#endif
    if (g_handle.attached) {
        return true;
    }
    std::byte* const picker = scan_main_image_unique(kPickerSignature, "slice_set_target_picker");
    if (picker == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=slice_set result=fail reason=target");
        return false;
    }
#if defined(RESTORATION_NATIVE_ORBIT_SLICE_SET) && RESTORATION_NATIVE_ORBIT_SLICE_SET
    if (native_orbit_asset_policy::omission_allowed_for_assets(
            native_orbit_asset_policy::installed_assets_match())) {
        core::log::write(core::log::Channel::client,core::log::Level::info,
            "ev=bootflow stage=slice_set result=native detours=0 authored_alias=11E694B6 assets=verified");
        return true;
    }
    core::log::write(core::log::Channel::client,core::log::Level::info,
        "ev=bootflow stage=slice_set result=fallback reason=authored_asset_identity detours=1");
#endif
    const hooking::detour::Spec spec{picker, reinterpret_cast<void*>(&pick_target)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=slice_set result=fail reason=attach");
        return false;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=slice_set result=ok");
    return true;
}

void uninstall_orbit_slice_set() noexcept {
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
}

} // namespace dawn::client::hooks::bootflow
