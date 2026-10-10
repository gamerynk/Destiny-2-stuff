#pragma once

#include <cstddef>

namespace dawn::client::targets::game::config_getter {

struct Targets {
    std::byte* url{};
    std::byte* token{};
};

void clear() noexcept;

[[nodiscard]] const Targets& get() noexcept;

[[nodiscard]] bool is_resolved() noexcept;

} // namespace dawn::client::targets::game::config_getter
