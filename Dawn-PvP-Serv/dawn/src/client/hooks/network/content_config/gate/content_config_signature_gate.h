#pragma once

#include <cstddef>

namespace dawn::client::hooks::network::content_config::gate {

struct Ownership final {
    std::byte* address{};
    char previous{};
    bool owned{};
};

[[nodiscard]] bool disable(std::byte* address, Ownership& ownership) noexcept;

[[nodiscard]] bool reassert(Ownership& ownership) noexcept;

[[nodiscard]] bool restore(Ownership& ownership) noexcept;

[[nodiscard]] bool is_disabled(const Ownership& ownership) noexcept;

} // namespace dawn::client::hooks::network::content_config::gate
