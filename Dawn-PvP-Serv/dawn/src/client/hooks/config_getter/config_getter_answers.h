#pragma once

#include <string_view>

namespace dawn::client::hooks::config_getter {

[[nodiscard]] bool publish_server_guid(std::string_view guid) noexcept;

[[nodiscard]] void* url_entry_point() noexcept;

[[nodiscard]] void* token_entry_point() noexcept;

} // namespace dawn::client::hooks::config_getter
