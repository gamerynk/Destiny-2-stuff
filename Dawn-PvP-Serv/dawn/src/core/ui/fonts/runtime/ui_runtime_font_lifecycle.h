#pragma once

#include <Windows.h>

#include <cstddef>

namespace dawn::core::ui::fonts::runtime {

inline constexpr float kAuthoredBasePixelSize = 16.0F;

enum class Source {
    unavailable,
    embeddedDefault,
    installed,
};

struct Snapshot {
    bool initialized{};
    Source source{Source::unavailable};
    std::size_t installedByteCount{};
    float basePixelSize{};
    float scale{};
};

[[nodiscard]] bool initialize(HMODULE module,
                              float basePixelSize = kAuthoredBasePixelSize) noexcept;

[[nodiscard]] bool apply_scale(float scale) noexcept;

[[nodiscard]] bool shutdown() noexcept;

[[nodiscard]] Snapshot snapshot() noexcept;

} // namespace dawn::core::ui::fonts::runtime
