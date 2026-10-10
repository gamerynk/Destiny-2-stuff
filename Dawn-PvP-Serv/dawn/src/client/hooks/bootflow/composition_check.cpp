#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "internal.h"

namespace dawn::client::hooks::bootflow {
#if !defined(RESTORATION_NATIVE_COMPOSITION_POLICY)
namespace {

// the matchmaking composition check; its prologue repeats across the image, so the pattern runs
// on to the properties byte read, which is unique. Branch displacements are wildcarded
constexpr std::string_view kCheckSignatureText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA 48 8B F9 48 85 C9 0F 84 ? ? ? ? 48 85 D2 0F 84 ? ? ? ? "
    "0F B6 82 A2 02 00 00";
constexpr auto kCheckSignature =
    signature<signature_length(kCheckSignatureText)>(kCheckSignatureText);

struct PropertiesLayout {
    static constexpr std::size_t bigFireteamCount = 20;
};

// native producer 17E39A0 compares exactly 0x2A8 bytes and bumps the properties version on
// change; never edit that caller-owned record
constexpr std::size_t kPropertiesBytes = 0x2A8;

constexpr std::int32_t kSolo = 0;

constexpr std::int64_t kNullArgument = 3;

constexpr unsigned kMaxReports = 4;

constexpr std::size_t kLineCapacity = 96;

using Check = std::int64_t(__fastcall*)(void*, std::byte*);

hooking::detour::Handle g_handle{};
std::atomic<Check> g_original{nullptr};
std::atomic<unsigned> g_reported{0};

void report(std::int32_t count) noexcept {
    if (g_reported.fetch_add(1, std::memory_order_relaxed) >= kMaxReports) {
        return;
    }
    std::array<char, kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=bootflow stage=composition result=private_snapshot count=%d",
                                      static_cast<int>(count));
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

// the old in-place write changed live properties, so the next producer rebuild bumped the
// version and cancelled the in-flight Tower search; the native predicate reads this record synchronously
__declspec(noinline) std::int64_t __fastcall check(void* config, std::byte* props) noexcept {
    const Check original = g_original.load(std::memory_order_acquire);
    if (original == nullptr) {
        return kNullArgument;
    }
    if (props == nullptr) {
        return original(config, props);
    }
    std::int32_t count = 0;
    std::memcpy(&count, props + PropertiesLayout::bigFireteamCount, sizeof count);
    if (count == kSolo) {
        return original(config, props);
    }
    std::array<std::byte, kPropertiesBytes> snapshot{};
    std::memcpy(snapshot.data(), props, snapshot.size());
    std::memcpy(snapshot.data() + PropertiesLayout::bigFireteamCount, &kSolo, sizeof kSolo);
    report(count);
    return original(config, snapshot.data());
}

} // namespace
#endif

bool install_composition_check() noexcept {
#if defined(RESTORATION_NATIVE_COMPOSITION_POLICY)
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=composition result=native detours=0 variant=1");
    return true;
#else
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kCheckSignature, "matchmaking_composition");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=composition result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&check)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=composition result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<Check>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=composition result=ok");
    return true;
#endif
}

void uninstall_composition_check() noexcept {
#if !defined(RESTORATION_NATIVE_COMPOSITION_POLICY)
    if (g_handle.attached) {
        (void)hooking::detour::uninstall(g_handle);
    }
    g_original.store(nullptr, std::memory_order_release);
    g_reported.store(0, std::memory_order_release);
#endif
}

} // namespace dawn::client::hooks::bootflow
