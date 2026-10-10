#pragma once
#include <cstdint>
#include <string_view>
namespace dawn::client::hooks::external_server::diagnostic {
// origin and path only; query, fragment and userinfo stay out of diagnostics
[[nodiscard]] constexpr std::string_view safe_url(std::string_view value) noexcept {
    value = value.substr(0, value.find_first_of("?#\r\n"));
    const auto scheme = value.find("://");
    if (scheme != std::string_view::npos) {
        const auto start = scheme + 3;
        const auto end = value.find('/', start);
        if (value.substr(start, end == std::string_view::npos ? end : end - start)
                .find('@') != std::string_view::npos) return "<redacted-url>";
    }
    return value;
}
[[nodiscard]] constexpr bool main_image_rva(std::uintptr_t base, std::uint32_t size,
                                           std::uintptr_t address,
                                           std::uint32_t& output) noexcept {
    if (address < base || address - base >= size) return false;
    output = static_cast<std::uint32_t>(address - base);
    return true;
}
}
