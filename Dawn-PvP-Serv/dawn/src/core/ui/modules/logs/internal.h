#pragma once

#include <Windows.h>

#include <string_view>

#include "../../../logging/view/log_snapshot_view.h"

namespace dawn::core::ui::modules::logs::internal {

void draw() noexcept;

void reset() noexcept;

enum class CopyStatus : unsigned char {
    idle,
    pending,
    copied,
    failed,
};

[[nodiscard]] constexpr bool
copy_status_fits_inline(float availableWidth, float spacing, float statusWidth) noexcept {
    return availableWidth >= spacing + statusWidth;
}

using CopyAction = bool (*)(HWND owner, std::string_view payload) noexcept;

// queued only; Win32 must not be called from the render lock
[[nodiscard]] bool request_copy(const log::view::Result& visible) noexcept;

[[nodiscard]] CopyStatus copy_status() noexcept;

[[nodiscard]] bool dispatch_pending_copy(HWND owner, CopyAction action) noexcept;

void dispatch_pending_copy(HWND owner) noexcept;

void cancel_pending_copy() noexcept;

} // namespace dawn::core::ui::modules::logs::internal
