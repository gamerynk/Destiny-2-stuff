#include <Windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include "client/hooks/bootflow/special_ammo_handoff.h"
static_assert(RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL == 1);
namespace P = dawn::client::hooks::bootflow::special_ammo_handoff;
namespace D = dawn::client::hooking::detour;
unsigned attachCalls{}, detachCalls{}, checks{}, ordinaryCalls{}, helperCalls{};
std::vector<std::string> logs;
namespace dawn::core::log {
void write(Channel, Level, std::string_view message) noexcept { logs.emplace_back(message); }
}
namespace dawn::client::hooking::detour {
bool install(std::span<const Spec>, std::span<Handle>) noexcept { ++attachCalls; return false; }
UninstallResult uninstall(std::span<Handle>, std::span<const ProtectedCodeEntry>, IdleCheck) noexcept {
    ++detachCalls; return UninstallResult::protectedCodeActive;
}
}
void __fastcall ordinary_collect(void*, std::uint32_t) noexcept { ++ordinaryCalls; }
void __fastcall native_helper(std::uint32_t) noexcept { ++helperCalls; }
bool check(bool value, const char* label) {
    ++checks; if (!value) std::fprintf(stderr, "FAIL %s\n", label); return value;
}
bool last_contains(const char* value) { return !logs.empty() && logs.back().find(value) != std::string::npos; }
int main() {
    auto* image = static_cast<std::byte*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) return 2;
    std::array<std::byte,4096> before{};
    for (std::size_t i = 0; i < before.size(); ++i) before[i] = std::byte(i & 255U);
    std::memcpy(image, before.data(), before.size());
    bool ok = check(P::hookCount == 0, "full trial selects zero hooks");
    ordinary_collect(nullptr, 1);
    P::install_for_image(image);
    ok &= check(last_contains("hooks=0") && last_contains("unvalidated=1"), "native-only bounded trial log");
    P::install_for_image(image);
    P::install_for_image(nullptr);
    ordinary_collect(nullptr, 1);
    ok &= check(ordinaryCalls == 2, "ordinary direct call remains ordinary; no installed interception");
    ok &= check(attachCalls == 0 && detachCalls == 0, "no installer or fallback invocation");
    ok &= check(!P::enabled && !P::imageBase, "no enabled policy or foreign image published");
    ok &= check(!P::original && !P::originalQuery && !P::originalAmmoQuery && !P::originalTransfer, "no original trampoline state");
    ok &= check(!P::nativeEligible && !P::nativeHandoff && !P::nativeSimulation && !P::nativeMethodLookup, "no callable native helper state");
    for (const auto& h : P::handles)
        ok &= check(!h.attached && !h.original && !h.replacement, "typed fallback storage inactive");
    ok &= check(std::memcmp(image, before.data(), before.size()) == 0, "foreign image bytes untouched");
    ok &= check(P::uninstall() && P::uninstall() && detachCalls == 0, "unselected cleanup is idempotent");

    P::handles.back() = {reinterpret_cast<void*>(&ordinary_collect), reinterpret_cast<void*>(&ordinary_collect), true};
    P::enabled = true; P::original = &ordinary_collect;
    P::install_for_image(image);
    ok &= check(last_contains("result=refused") && !last_contains("hooks=0"), "preexisting attachment refuses native-only install claim");
    ok &= check(!P::uninstall() && last_contains("result=refused"), "preexisting owner cannot be retired with zero protected entries");
    ok &= check(P::enabled && P::handles.back().attached && P::original == &ordinary_collect, "refusal preserves preexisting owner behavior");
    ok &= check(attachCalls == 0 && detachCalls == 0, "refusal performs no implicit attach/detach");
    P::handles.back() = {}; P::enabled = false; P::original = nullptr;

    P::nativeHandoff = &native_helper;
    P::install_for_image(image);
    ok &= check(last_contains("result=refused") && helperCalls == 0, "stale helper capability refused without invocation");
    ok &= check(P::uninstall() && !P::nativeHandoff, "inactive helper state safely cleared");
    P::activeCalls = 1; P::enabled = true; P::original = &ordinary_collect;
    P::install_for_image(image);
    ok &= check(last_contains("result=refused") && !P::uninstall(), "active owner defers installation and cleanup");
    ok &= check(P::enabled && P::original == &ordinary_collect, "active owner behavior remains unchanged");
    P::activeCalls = 0;
    ok &= check(P::uninstall() && !P::enabled && !P::original, "quiescent cleanup clears inactive state");
    P::install_for_image(image);
    ok &= check(last_contains("hooks=0") && attachCalls == 0 && detachCalls == 0 && helperCalls == 0, "native trial can retry after owner retirement");
    ok &= check(std::memcmp(image, before.data(), before.size()) == 0, "entry bytes remain untouched through retries");
    VirtualFree(image, 0, MEM_RELEASE);
    std::printf("%s %u actual-header full-ammo omission checks observer=%d trace=%d; no target game/process access\n",
        ok ? "PASS" : "FAIL", checks, RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL, RESTORATION_HOOK_REDUCTION_TRACE);
    return ok ? 0 : 1;
}
