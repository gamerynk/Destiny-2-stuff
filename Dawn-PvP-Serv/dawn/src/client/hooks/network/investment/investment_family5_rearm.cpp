// the account's unlock overrides reach the family-five object only when its commit
// finishes, so a rebuild armed earlier reads an override list that isn't there yet

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "../../../../core/logging/log.h"
#include "../../../hooking/detour.h"
#include "internal.h"

namespace dawn::client::hooks::network::investment {
#if defined(RESTORATION_NATIVE_INVESTMENT_INVALIDATION)
bool install_family5_rearm() noexcept {
    return true;
}

bool uninstall_family5_rearm() noexcept {
    return true;
}

bool family5_rearm_is_installed() noexcept {
    return false;
}
#else
namespace {

// the prologue alone matches a dozen sites differing only in call displacements, so the
// exact trailing run makes it unique; every relative displacement is a wildcard
constexpr std::string_view kCommitSignatureText =
    "48 89 5C 24 ? 57 48 83 EC ? 48 8B DA 48 8B F9 E8 ? ? ? ? 48 8B C8 48 8B D3 E8 ? ? ? ? 48 8D "
    "44 24 ? C6 47 50 02 A8 03 75 ?";
constexpr auto kCommitSignature =
    signature<signature_length(kCommitSignatureText)>(kCommitSignatureText);

constexpr std::int64_t kNoCommit = 0;

using CommitFamily5 = std::int64_t(__fastcall*)(void*, std::uint64_t*);

hooking::detour::Handle g_handle{};
std::atomic<CommitFamily5> g_original{nullptr};
std::atomic_bool g_reportedArm{false};

// the two callers pass different second arguments, so pass it on unread; arming twice is
// harmless since the next freshness verdict uses it up
__declspec(noinline) std::int64_t __fastcall commit(void* primaryRecordBlock,
                                                    std::uint64_t* nested4) noexcept {
    const CommitFamily5 original = g_original.load(std::memory_order_acquire);
    if (original == nullptr) {
        return kNoCommit;
    }
    // arm on the way out: the overrides are in the object only once the commit has run
    const std::int64_t result = original(primaryRecordBlock, nested4);
    arm_derived_rebuild();
    if (!g_reportedArm.exchange(true, std::memory_order_relaxed)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=investment stage=family5_commit result=armed");
    }
    return result;
}

} // namespace

bool install_family5_rearm() noexcept {
    if (g_handle.attached) {
        return true;
    }
    std::byte* const target = scan_main_image_unique(kCommitSignature, "queuez_family5_commit");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=family5_commit result=fail reason=target");
        return false;
    }
    const hooking::detour::Spec spec{target, reinterpret_cast<void*>(&commit)};
    if (!hooking::detour::install(spec, g_handle)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=investment stage=family5_commit result=fail reason=attach");
        return false;
    }
    g_original.store(reinterpret_cast<CommitFamily5>(g_handle.original), std::memory_order_release);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=investment stage=family5_commit result=ok");
    return true;
}

bool uninstall_family5_rearm() noexcept {
    if (g_handle.attached && !hooking::detour::uninstall(g_handle)) {
        return false;
    }
    g_original.store(nullptr, std::memory_order_release);
    g_reportedArm.store(false, std::memory_order_release);
    return true;
}

bool family5_rearm_is_installed() noexcept {
    return g_handle.attached;
}
#endif

} // namespace dawn::client::hooks::network::investment
