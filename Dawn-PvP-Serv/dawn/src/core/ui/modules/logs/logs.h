#pragma once

#include <Windows.h>

namespace dawn::core::ui::modules::logs {

[[nodiscard]] bool initialize() noexcept;

void shutdown() noexcept;

void dispatch_pending_copy(HWND owner) noexcept;

} // namespace dawn::core::ui::modules::logs
