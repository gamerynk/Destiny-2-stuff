#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include "../../middleware/content/bootstrap_configuration.h"

namespace dawn::server::http::content_configuration {
struct Snapshot final {
    std::array<char, middleware::content::bootstrap_configuration::kUrlCapacity> url{};
    std::array<char, 37> guid{};
};
[[nodiscard]] bool publish(std::uint64_t device, std::uint64_t platform,
                           std::string_view url) noexcept;
[[nodiscard]] bool snapshot(std::uint64_t device, std::uint64_t platform,
                            Snapshot& output) noexcept;
void clear() noexcept;
}
