#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <string_view>

#include "../../../core/logging/log.h"
#include "internal.h"

namespace dawn::client::hooks::bootflow {
namespace {

constexpr std::string_view kStepSignatureText =
    "48 83 EC 28 E8 ? ? ? ? 48 85 C0 74 0B 8B 80 90 03 00 00 48 83 C4 28 C3 83 C8 FF 48 83 C4 28 "
    "C3";
constexpr auto kStepSignature = signature<signature_length(kStepSignatureText)>(kStepSignatureText);
constexpr std::int32_t kInWorld = 38;
constexpr std::int32_t kNoStep = -1;
constexpr std::uint64_t kStepStaleMs = 1'000;

using GetStep = std::int64_t(__fastcall*)() noexcept;

std::atomic<GetStep> g_step{nullptr};
std::atomic_int32_t g_publishedStep{kNoStep};
std::atomic_uint64_t g_publishedTick{0};
SRWLOCK g_stepLifetime=SRWLOCK_INIT;

[[nodiscard]] std::int32_t read_step() noexcept {
    const GetStep read = g_step.load(std::memory_order_acquire);
    if (read==nullptr) return kNoStep;
    __try { return static_cast<std::int32_t>(read() & 0xFFFFFFFF); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return kNoStep; }
}

} // namespace

void poll_world_step() noexcept {
    AcquireSRWLockExclusive(&g_stepLifetime);
    if (g_step.load(std::memory_order_acquire)!=nullptr) {
        g_publishedStep.store(read_step(), std::memory_order_relaxed);
        g_publishedTick.store(GetTickCount64(), std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&g_stepLifetime);
}

bool in_world() noexcept {
    AcquireSRWLockShared(&g_stepLifetime);
    const std::uint64_t published = g_publishedTick.load(std::memory_order_acquire);
    const bool current=g_publishedStep.load(std::memory_order_relaxed)==kInWorld
                       && published!=0 && GetTickCount64()-published<kStepStaleMs;
    ReleaseSRWLockShared(&g_stepLifetime);
    return current;
}

bool install_world_presence() noexcept {
    std::byte* const target = scan_main_image_unique(kStepSignature, "bootflow_current_step");
    if (target == nullptr) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=bootflow stage=world_presence result=fail reason=target");
        return false;
    }
    AcquireSRWLockExclusive(&g_stepLifetime);
    g_step.store(reinterpret_cast<GetStep>(target), std::memory_order_release);
    ReleaseSRWLockExclusive(&g_stepLifetime);
    core::log::write(core::log::Channel::client,
                     core::log::Level::info,
                     "ev=bootflow stage=world_presence result=ok");
    return true;
}

void uninstall_world_presence() noexcept {
    AcquireSRWLockExclusive(&g_stepLifetime);
    g_step.store(nullptr, std::memory_order_release);
    g_publishedStep.store(kNoStep, std::memory_order_relaxed);
    g_publishedTick.store(0, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_stepLifetime);
}

} // namespace dawn::client::hooks::bootflow
