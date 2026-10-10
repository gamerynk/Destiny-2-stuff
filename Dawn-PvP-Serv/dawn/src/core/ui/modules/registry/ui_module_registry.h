#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../ui_module_descriptor.h"

namespace dawn::core::ui::modules::registry {

inline constexpr std::size_t kModuleCapacity = 32;

enum class RegistrationResult : std::uint8_t {
    registered,
    invalidDescriptor,
    duplicateStableId,
    capacityReached,
};

class RegistrySnapshot final {
public:
    [[nodiscard]] std::span<const Descriptor> entries() const noexcept;

    [[nodiscard]] std::uint64_t revision() const noexcept;

private:
    friend RegistrySnapshot snapshot() noexcept;

    std::array<Descriptor, kModuleCapacity> entries_{};
    std::size_t count_{};
    std::uint64_t revision_{};
};

[[nodiscard]] RegistrationResult register_module(const Descriptor& descriptor) noexcept;

[[nodiscard]] bool unregister_module(std::string_view stableId) noexcept;

[[nodiscard]] RegistrySnapshot snapshot() noexcept;

// each part holds its own slot instead of calling the registry, so registering twice does
// nothing and shutdown is the exact reverse
class PageRegistration final {
public:
    [[nodiscard]] bool acquire(Owner owner,
                               std::string_view stableId,
                               std::string_view displayName,
                               FrameCallback callback,
                               void (*prepare)() noexcept = nullptr) noexcept;

    void release(void (*finish)() noexcept = nullptr) noexcept;

private:
    // guarded by a lock in the .cpp to keep Windows.h out of this header
    bool registered_{};
    std::string_view stableId_{};
};

void clear(Owner owner) noexcept;

void clear() noexcept;

void shutdown() noexcept;

} // namespace dawn::core::ui::modules::registry
