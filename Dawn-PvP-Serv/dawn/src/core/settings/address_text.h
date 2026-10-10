#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dawn::core::settings::address {

inline constexpr std::size_t kOctets = 4;

// host names are refused; every consumer forwards this as a numeric address
[[nodiscard]] bool parse_ipv4(std::string_view text,
                              std::array<unsigned char, kOctets>& output) noexcept;

} // namespace dawn::core::settings::address
