#pragma once

namespace dawn::client::graphics {

// Wine defers attaching its display until something asks for it, and the renderer expects it
// already attached. Harmless on Windows
void initialize_wine_display() noexcept;

} // namespace dawn::client::graphics
