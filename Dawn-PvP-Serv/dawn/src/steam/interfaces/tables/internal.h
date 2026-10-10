#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>

#include "../internal.h"

namespace dawn::steam::interfaces::tables {

struct InterfaceObject {
    FARPROC* methods{};
};

template <typename Slot> [[nodiscard]] constexpr std::size_t index(Slot slot) noexcept {
    return static_cast<std::size_t>(slot);
}

template <std::size_t Count> void fill_empty(std::array<FARPROC, Count>& table) noexcept {
    table.fill(reinterpret_cast<FARPROC>(&methods::empty));
}

template <typename Function> void set_method(FARPROC& slot, Function function) noexcept {
    slot = reinterpret_cast<FARPROC>(function);
}

void initialize_common() noexcept;

void initialize_networking() noexcept;

} // namespace dawn::steam::interfaces::tables
