#pragma once

#include <cstddef>

namespace dawn::client::targets::game::assert_handler {

using Setter = void(__fastcall*)(void*);

struct Targets {
    std::byte** slot{};
    void* original{};
    Setter setter{};
};

void clear() noexcept;

[[nodiscard]] const Targets& get() noexcept;

[[nodiscard]] bool is_resolved() noexcept;

} // namespace dawn::client::targets::game::assert_handler
