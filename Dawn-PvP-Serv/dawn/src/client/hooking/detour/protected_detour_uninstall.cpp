#include <Windows.h>

#include <array>
#include <detours.h>

#include "../detour.h"
#include "transaction/detour_thread_transaction.h"

namespace dawn::client::hooking::detour {
namespace {

constexpr std::size_t kBatchLimit = 32;

[[nodiscard]] bool valid_batch(std::span<const Handle> handles) noexcept {
    if (handles.empty() || handles.size() > kBatchLimit) {
        return false;
    }
    for (const Handle& handle : handles) {
        if (!handle.attached || handle.original == nullptr || handle.replacement == nullptr) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool queue_detach(std::span<const Handle> handles,
                                std::array<void*, kBatchLimit>& originals) noexcept {
    originals = {};
    for (std::size_t index = 0; index < handles.size(); ++index) {
        originals[index] = handles[index].original;
        if (DetourDetach(&originals[index], handles[index].replacement) != NO_ERROR) {
            return false;
        }
    }
    return true;
}

void clear_handles(std::span<Handle> handles) noexcept {
    for (Handle& handle : handles) {
        handle = {};
    }
}

} // namespace

UninstallResult uninstall(Handle& handle,
                          std::span<const ProtectedCodeEntry> protectedEntries) noexcept {
    return uninstall(std::span(&handle, 1), protectedEntries);
}

UninstallResult uninstall(Handle& handle,
                          std::span<const ProtectedCodeEntry> protectedEntries,
                          IdleCheck idleCheck) noexcept {
    return uninstall(std::span(&handle, 1), protectedEntries, idleCheck);
}

UninstallResult uninstall(std::span<Handle> handles,
                          std::span<const ProtectedCodeEntry> protectedEntries) noexcept {
    return uninstall(handles, protectedEntries, nullptr);
}

UninstallResult uninstall(std::span<Handle> handles,
                          std::span<const ProtectedCodeEntry> protectedEntries,
                          IdleCheck idleCheck) noexcept {
    if (!valid_batch(handles) || protectedEntries.empty()) {
        return UninstallResult::failed;
    }

    transaction::Threads threads;
    if (!transaction::begin(threads)) {
        return UninstallResult::failed;
    }

    const transaction::InspectionResult inspection =
        transaction::inspect(threads, protectedEntries);
    if (inspection != transaction::InspectionResult::clear) {
        const bool aborted = transaction::abort(threads);
        if (!aborted || inspection == transaction::InspectionResult::failed) {
            return UninstallResult::failed;
        }
        return UninstallResult::protectedCodeActive;
    }
    if (idleCheck != nullptr && !idleCheck()) {
        return transaction::abort(threads) ? UninstallResult::protectedCodeActive
                                           : UninstallResult::failed;
    }

    std::array<void*, kBatchLimit> originals{};
    if (!queue_detach(handles, originals)) {
        (void)transaction::abort(threads);
        return UninstallResult::failed;
    }
    if (!transaction::commit(threads)) {
        return UninstallResult::failed;
    }

    clear_handles(handles);
    return UninstallResult::removed;
}

} // namespace dawn::client::hooking::detour
