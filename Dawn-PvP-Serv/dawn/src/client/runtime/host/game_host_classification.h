#pragma once

#include <string_view>

namespace dawn::client::runtime::host {

enum class NetworkRequirement {
    required,
    notApplicable,
};

[[nodiscard]] NetworkRequirement classify(std::wstring_view executablePath) noexcept;

[[nodiscard]] NetworkRequirement current_requirement() noexcept;

} // namespace dawn::client::runtime::host
