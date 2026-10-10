#pragma once

#include <cstddef>

namespace dawn::client::targets::game::retail_log {

struct Targets {
    std::byte* enqueue{};
    std::byte* setCategoryVerbosity{};
};

void clear() noexcept;

[[nodiscard]] const Targets& get() noexcept;

[[nodiscard]] bool is_resolved() noexcept;

} // namespace dawn::client::targets::game::retail_log
