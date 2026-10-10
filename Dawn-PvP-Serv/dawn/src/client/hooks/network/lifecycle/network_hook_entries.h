#pragma once

#include <array>

#include "network_hook_group.h"

namespace dawn::client::hooks::network::lifecycle {

using PlatformSpecs = std::array<hooking::detour::Spec, kPlatformSlots.size()>;
using PlatformProtectedEntries =
    std::array<hooking::detour::ProtectedCodeEntry, kPlatformSlots.size() + 2>;

[[nodiscard]] PlatformSpecs platform_specs() noexcept;

[[nodiscard]] PlatformProtectedEntries platform_protected_entries() noexcept;

} // namespace dawn::client::hooks::network::lifecycle
