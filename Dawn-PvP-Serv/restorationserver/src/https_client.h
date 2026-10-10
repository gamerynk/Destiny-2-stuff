#pragma once

#include <string>
#include <string_view>

namespace restoration::https_client {

struct Response final {
    unsigned status{};
    std::string body;
};

[[nodiscard]] bool initialize() noexcept;

void shutdown() noexcept;

// blocks for seconds; never call on the server service thread
[[nodiscard]] bool get(std::wstring_view host,
                       std::wstring_view path,
                       std::wstring_view headers,
                       Response& output) noexcept;

// printable ASCII only, so callers can't smuggle headers
[[nodiscard]] bool widen_ascii(std::string_view input, std::wstring& output) noexcept;

} // namespace restoration::https_client
