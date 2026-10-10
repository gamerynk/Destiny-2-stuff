#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace dawn::core::settings::upgrade {

[[nodiscard]] bool needed(std::string_view document) noexcept;

[[nodiscard]] bool apply(std::string_view document,
                         std::string_view bundled,
                         std::span<char> output,
                         std::size_t& written) noexcept;

} // namespace dawn::core::settings::upgrade
