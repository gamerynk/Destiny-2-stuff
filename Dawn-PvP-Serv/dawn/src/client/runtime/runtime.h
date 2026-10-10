#pragma once

#include <Windows.h>

namespace dawn::client {

[[nodiscard]] bool initialize(void* module) noexcept;

[[nodiscard]] bool activate_main_once() noexcept;

[[nodiscard]] bool activate_graphics_once() noexcept;

[[nodiscard]] bool activate_platform_once(HMODULE networkingModule) noexcept;

[[nodiscard]] bool shutdown() noexcept;

} // namespace dawn::client
