#pragma once

#include <string_view>

#include "../internal.h"

namespace dawn::state::content_manifest::scanner::package {

[[nodiscard]] bool read_header(std::wstring_view directory, Candidate& candidate) noexcept;

} // namespace dawn::state::content_manifest::scanner::package
