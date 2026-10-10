#pragma once

#include <Windows.h>

#include <cstddef>

namespace dawn::core::ui::fonts::installed {

struct DataView {
    void* bytes{};
    int byteCount{};
};

[[nodiscard]] bool load(HMODULE module, DataView& output) noexcept;

void clear() noexcept;

[[nodiscard]] std::size_t byte_count() noexcept;

} // namespace dawn::core::ui::fonts::installed
