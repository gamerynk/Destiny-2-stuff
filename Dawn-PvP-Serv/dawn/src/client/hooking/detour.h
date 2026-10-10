#pragma once

#include <span>

namespace dawn::client::hooking::detour {

struct Spec {
    void* target{};
    void* replacement{};
};

struct Handle {
    void* original{};
    void* replacement{};
    bool attached{};
};

struct ProtectedCodeEntry {
    void* address{};
};

using IdleCheck = bool (*)() noexcept;

enum class UninstallResult {
    removed,
    protectedCodeActive,
    failed,
};

[[nodiscard]] bool install(const Spec& spec, Handle& output) noexcept;

[[nodiscard]] bool uninstall(Handle& handle) noexcept;

[[nodiscard]] bool uninstall(Handle& handle, bool& replacementActive) noexcept;

[[nodiscard]] UninstallResult
uninstall(Handle& handle, std::span<const ProtectedCodeEntry> protectedEntries) noexcept;

[[nodiscard]] UninstallResult uninstall(Handle& handle,
                                        std::span<const ProtectedCodeEntry> protectedEntries,
                                        IdleCheck idleCheck) noexcept;

[[nodiscard]] bool install(std::span<const Spec> specs, std::span<Handle> outputs) noexcept;

[[nodiscard]] bool uninstall(std::span<Handle> handles) noexcept;

[[nodiscard]] bool uninstall(std::span<Handle> handles, bool& replacementActive) noexcept;

[[nodiscard]] UninstallResult
uninstall(std::span<Handle> handles, std::span<const ProtectedCodeEntry> protectedEntries) noexcept;

[[nodiscard]] UninstallResult uninstall(std::span<Handle> handles,
                                        std::span<const ProtectedCodeEntry> protectedEntries,
                                        IdleCheck idleCheck) noexcept;

} // namespace dawn::client::hooking::detour
