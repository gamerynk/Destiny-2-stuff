#include "detour.h"

#include <Windows.h>

#include <array>
#include <detours.h>
#include <cstdio>
#include "../../core/logging/log.h"

#include "detour/transaction/detour_thread_transaction.h"

namespace dawn::client::hooking::detour {
namespace {

constexpr std::size_t kBatchLimit = 32;

void report_failure(const char* stage, LONG error, std::size_t hookIndex = kBatchLimit) noexcept {
    const auto transaction = transaction::last_failure();
    std::array<char, 256> line{};
    const int size = std::snprintf(line.data(), line.size(),
        "ev=detour_install result=fail stage=%s error=%ld hook_index=%zu"
        " thread_count=%zu thread_id=%lu",
        stage != nullptr ? stage : transaction.stage,
        error != NO_ERROR ? error : transaction.error, hookIndex,
        transaction.threadCount, static_cast<unsigned long>(transaction.threadId));
    if (size > 0) core::log::write(core::log::Channel::client, core::log::Level::error,
                                 {line.data(), static_cast<std::size_t>(size)});
}

[[nodiscard]] bool valid_install_batch(std::span<const Spec> specs,
                                       std::span<const Handle> outputs) noexcept {
    if (specs.empty() || specs.size() != outputs.size() || specs.size() > kBatchLimit) {
        return false;
    }
    for (std::size_t index = 0; index < specs.size(); ++index) {
        if (outputs[index].attached || specs[index].target == nullptr
            || specs[index].replacement == nullptr) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_uninstall_batch(std::span<const Handle> handles) noexcept {
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

[[nodiscard]] bool uninstall_unprotected(std::span<Handle> handles) noexcept {
    if (!valid_uninstall_batch(handles)) {
        return false;
    }

    transaction::Threads threads;
    if (!transaction::begin(threads)) {
        return false;
    }

    std::array<void*, kBatchLimit> originals{};
    if (!queue_detach(handles, originals)) {
        (void)transaction::abort(threads);
        return false;
    }
    if (!transaction::commit(threads)) {
        return false;
    }

    clear_handles(handles);
    return true;
}

} // namespace

bool install(const Spec& spec, Handle& output) noexcept {
    return install(std::span(&spec, 1), std::span(&output, 1));
}

bool uninstall(Handle& handle) noexcept {
    return uninstall(std::span(&handle, 1));
}

bool uninstall(Handle& handle, bool& replacementActive) noexcept {
    return uninstall(std::span(&handle, 1), replacementActive);
}

bool install(std::span<const Spec> specs, std::span<Handle> outputs) noexcept {
    if (!valid_install_batch(specs, outputs)) {
        report_failure("validate_batch", ERROR_INVALID_PARAMETER);
        return false;
    }

    transaction::Threads threads;
    if (!transaction::begin(threads)) {
        report_failure(nullptr, NO_ERROR);
        return false;
    }

    std::array<void*, kBatchLimit> originals{};
    for (std::size_t index = 0; index < specs.size(); ++index) {
        originals[index] = specs[index].target;
        const LONG attachError = DetourAttach(&originals[index], specs[index].replacement);
        if (attachError != NO_ERROR) {
            (void)transaction::abort(threads);
            report_failure("attach", attachError, index);
            return false;
        }
    }
    if (!transaction::commit(threads)) {
        report_failure(nullptr, NO_ERROR);
        return false;
    }

    for (std::size_t index = 0; index < specs.size(); ++index) {
        outputs[index].original = originals[index];
        outputs[index].replacement = specs[index].replacement;
        outputs[index].attached = true;
    }
    return true;
}

bool uninstall(std::span<Handle> handles) noexcept {
    return uninstall_unprotected(handles);
}

bool uninstall(std::span<Handle> handles, bool& replacementActive) noexcept {
    replacementActive = false;
    if (handles.empty() || handles.size() > kBatchLimit) {
        return false;
    }

    std::array<ProtectedCodeEntry, kBatchLimit> entries{};
    for (std::size_t index = 0; index < handles.size(); ++index) {
        entries[index].address = handles[index].replacement;
    }
    const UninstallResult result = uninstall(handles, std::span(entries.data(), handles.size()));
    replacementActive = result == UninstallResult::protectedCodeActive;
    return result == UninstallResult::removed;
}

} // namespace dawn::client::hooking::detour
