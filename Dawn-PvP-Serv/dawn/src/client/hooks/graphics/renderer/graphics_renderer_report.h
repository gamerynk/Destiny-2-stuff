#pragma once

namespace dawn::client::hooks::graphics::renderer::report {

enum class Stage : unsigned {
    none,
    acquire,
    target,
    init,
    frame,
    shutdown,
};

enum class Reason : unsigned {
    none,
    window,
    windowThread,
    windowClass,
    description,
    device,
    context,
    deviceRemoved,
    backBuffer,
    view,
    viewFormat,
    layout,
    win32Backend,
    dx11Backend,
    windowInput,
    fontScale,
    deviceLost,
    rebuildTarget,
    surfaceLost,
};

void note(Stage stage, Reason reason) noexcept;

void note_active() noexcept;

} // namespace dawn::client::hooks::graphics::renderer::report
