#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../../hooking/detour.h"
#include "internal.h"
#include "tower_search_completion_policy.h"

namespace dawn::client::hooks::bootflow {
#if !defined(RESTORATION_NATIVE_TOWER_COMPLETION)
namespace {
namespace policy = tower_search_completion_policy;

constexpr std::size_t kCompletionRva = 0xEE91C0;
constexpr std::size_t kInterfaceGetterRva = 0xF17C70;
constexpr std::size_t kRootGetterRva = 0xE37990;
constexpr std::string_view kCompletionSignatureText =
    "48 89 5C 24 18 55 56 57 48 8D AC 24 D0 FD FF FF 48 81 EC 30 03 00 00";
constexpr auto kCompletionSignature = signature<signature_length(kCompletionSignatureText)>(kCompletionSignatureText);
constexpr std::string_view kRootSignatureText =
    "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 20 48 8B 05 7F 68 9D 01 48 85 C0 0F 84 ? ? ? ?";
constexpr auto kRootSignature = signature<signature_length(kRootSignatureText)>(kRootSignatureText);

using Complete = bool(__fastcall*)(void*);
using Getter = std::byte*(__fastcall*)();
hooking::detour::Handle g_handle{};
std::atomic<Complete> g_original{nullptr};
Getter g_interface{};
Getter g_root{};
const std::byte* g_properties{};
std::atomic_bool g_waitReported{false};
std::atomic_bool g_releaseReported{false};

[[nodiscard]] bool pending_native_search() noexcept {
    std::array<std::byte, policy::kConfigSize> config{};
    std::array<std::byte, policy::kPropertiesSize> props{};
    std::array<std::byte, policy::kSearchPrefixSize> search{};
    __try {
        const std::byte* const online = g_interface();
        if (online == nullptr) {
            return false;
        }
        if (!middleware::content::social_activity_policy::find(
                policy::read<std::uint32_t>({online + 0x25C40, policy::kConfigSize}, 0x1F8))) {
            return false;
        }
        const std::byte* const root = g_root();
        if (root == nullptr) {
            return false;
        }
        std::memcpy(config.data(), online + 0x25C40, config.size());
        std::memcpy(props.data(), g_properties, props.size());
        std::memcpy(search.data(), root + 0x208 + 0x28, search.size());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return !policy::completion_ready(true, config, props, search);
}

__declspec(noinline) bool __fastcall complete(void* step) noexcept {
    const Complete original = g_original.load(std::memory_order_acquire);
    if (original == nullptr) {
        return false;
    }
    // always run the native pending-join/team/capacity logic first; never turn a native wait or failure into success
    if (!original(step)) {
        return false;
    }
    if (pending_native_search()) {
        if (!g_waitReported.exchange(true, std::memory_order_relaxed)) {
            core::log::write(core::log::Channel::client, core::log::Level::info,
                            "ev=bootflow stage=tower_search_completion result=pending_native_work");
        }
        return false;
    }
    if (g_waitReported.load(std::memory_order_relaxed)
        && !g_releaseReported.exchange(true, std::memory_order_relaxed)) {
        core::log::write(core::log::Channel::client, core::log::Level::info,
                        "ev=bootflow stage=tower_search_completion result=native_completion");
    }
    return true;
}

[[nodiscard]] bool dependencies_match(const std::byte* image) noexcept {
    constexpr unsigned char interfaceCode[]{0x48,0x8D,0x0D,0x51,0x90,0x8F,0x01,0xE9,0x74,0x27,0x0B,0x00};
    constexpr unsigned char configCode[]{0x48,0x63,0xC2,0x48,0x69,0xD0,0x88,0x03,0x00,0x00,
                                        0x48,0x8D,0x81,0x40,0x5C,0x02,0x00,0x48,0x03,0xC2,0xC3};
    __try {
        return std::memcmp(image + kInterfaceGetterRva, interfaceCode, sizeof interfaceCode) == 0
            && std::memcmp(image + 0xF17D40, configCode, sizeof configCode) == 0
            && image[0xEE91EB] == std::byte{0xE8}
            && resolve_relative(image + 0xEE91EC, image + 0xEE91F0) == image + kInterfaceGetterRva;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace
#endif

bool install_tower_search_completion() noexcept {
#if defined(RESTORATION_NATIVE_TOWER_COMPLETION)
    core::log::write(core::log::Channel::client, core::log::Level::info,
                    "ev=bootflow stage=tower_search_completion result=native detours=0 variant=1");
    return true;
#else
    if (g_handle.attached) {
        return true;
    }
    const auto image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    std::byte* const target = scan_main_image_unique(kCompletionSignature, "tower_native_completion");
    std::byte* const root = scan_main_image_unique(kRootSignature, "tower_native_matchmaking_getter");
    if (image == nullptr || target != image + kCompletionRva || root != image + kRootGetterRva
        || !dependencies_match(image)) {
        core::log::write(core::log::Channel::client, core::log::Level::warn,
                        "ev=bootflow stage=tower_search_completion result=fail reason=unsupported_image");
        return false;
    }
    g_interface = reinterpret_cast<Getter>(image + kInterfaceGetterRva);
    g_root = reinterpret_cast<Getter>(root);
    g_properties = image + 0x3213150;
    if (!hooking::detour::install({target, reinterpret_cast<void*>(&complete)}, g_handle)) {
        return false;
    }
    g_original.store(reinterpret_cast<Complete>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client, core::log::Level::info,
                    "ev=bootflow stage=tower_search_completion result=ok mode=native_pending_only");
    return true;
#endif
}

void uninstall_tower_search_completion() noexcept {
#if !defined(RESTORATION_NATIVE_TOWER_COMPLETION)
    if (g_handle.attached && !hooking::detour::uninstall(g_handle)) {
        return;
    }
    g_original.store(nullptr, std::memory_order_release);
    g_waitReported.store(false, std::memory_order_relaxed);
    g_releaseReported.store(false, std::memory_order_relaxed);
#endif
}
} // namespace dawn::client::hooks::bootflow
