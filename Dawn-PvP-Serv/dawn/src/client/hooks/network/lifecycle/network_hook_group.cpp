#include "network_hook_group.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "../coordinator/network_call_coordinator.h"

namespace dawn::client::hooks::network {

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<hooking::detour::Handle, kHandleCount> g_handles{};
// restored entries stay callable for replacements admitted before detach
std::array<void*, kHandleCount> g_targetEntries{};
std::array<bool, kHandleCount> g_accepting{};

} // namespace dawn::client::hooks::network

namespace dawn::client::hooks::network::lifecycle {
namespace {

constexpr std::array<const char*, kHandleCount> kSlotNames{
    "authentication_status",
    "set_certificate",
};
// a short table would zero-fill its tail and shift every later name onto the wrong slot
static_assert(kSlotNames.back() != nullptr);

void report_attach(HookSlot slot, bool attached) noexcept {
    const std::size_t index = static_cast<std::size_t>(slot);
    std::array<char, 128> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=hook stage=attach group=network name=%s result=%s",
                                      index < kSlotNames.size() ? kSlotNames[index] : "?",
                                      attached ? "ok" : "fail");
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         attached ? core::log::Level::debug : core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] constexpr std::size_t index(HookSlot slot) noexcept {
    return static_cast<std::size_t>(slot);
}

} // namespace

bool all_installed(std::span<const HookSlot> slots) noexcept {
    return !slots.empty() && std::all_of(slots.begin(), slots.end(), [](HookSlot slot) {
        return g_handles[index(slot)].attached;
    });
}

bool any_installed(std::span<const HookSlot> slots) noexcept {
    return std::any_of(
        slots.begin(), slots.end(), [](HookSlot slot) { return g_handles[index(slot)].attached; });
}

bool all_accepting(std::span<const HookSlot> slots) noexcept {
    return !slots.empty() && std::all_of(slots.begin(), slots.end(), [](HookSlot slot) {
        return g_accepting[index(slot)];
    });
}

void set_accepting(std::span<const HookSlot> slots, bool accepting) noexcept {
    for (const HookSlot slot : slots) {
        g_accepting[index(slot)] = accepting;
    }
}

bool install_group(std::span<const hooking::detour::Spec> specs,
                   std::span<const HookSlot> slots) noexcept {
    if (specs.empty() || specs.size() != slots.size() || specs.size() > kHandleCount) {
        return false;
    }

    std::array<hooking::detour::Handle, kHandleCount> installed{};
    std::array<void*, kHandleCount> previousEntries{};
    for (std::size_t offset = 0; offset < slots.size(); ++offset) {
        previousEntries[offset] = g_targetEntries[index(slots[offset])];
        g_targetEntries[index(slots[offset])] = specs[offset].target;
    }
    if (!hooking::detour::install(specs, std::span(installed).first(specs.size()))) {
        for (std::size_t offset = 0; offset < slots.size(); ++offset) {
            g_targetEntries[index(slots[offset])] = previousEntries[offset];
            report_attach(slots[offset], false);
        }
        return false;
    }

    // the exclusive lock blocks replacements until the whole group is visible
    for (std::size_t offset = 0; offset < slots.size(); ++offset) {
        g_handles[index(slots[offset])] = installed[offset];
        g_accepting[index(slots[offset])] = true;
        report_attach(slots[offset], installed[offset].attached);
    }
    return true;
}

bool uninstall_group(
    std::span<const HookSlot> slots,
    std::span<const hooking::detour::ProtectedCodeEntry> protectedEntries) noexcept {
    if (!any_installed(slots)) {
        set_accepting(slots, false);
        return true;
    }
    if (!all_installed(slots) || slots.size() > kHandleCount || protectedEntries.empty()
        || coordinator::active_calls() != 0) {
        return false;
    }

    std::array<hooking::detour::Handle, kHandleCount> attached{};
    for (std::size_t offset = 0; offset < slots.size(); ++offset) {
        attached[offset] = g_handles[index(slots[offset])];
    }
    const hooking::detour::UninstallResult result = hooking::detour::uninstall(
        std::span(attached).first(slots.size()), protectedEntries, &coordinator::idle);
    if (result != hooking::detour::UninstallResult::removed) {
        return false;
    }
    for (const HookSlot slot : slots) {
        g_handles[index(slot)] = {};
        g_accepting[index(slot)] = false;
    }
    return true;
}

} // namespace dawn::client::hooks::network::lifecycle
