#pragma once

#include <string_view>

#include "named_tags.h"

namespace dawn::middleware::content::packages {

[[nodiscard]] bool load_catalog(std::wstring_view directory,
                                named_tags::DirectoryResult& result) noexcept;

} // namespace dawn::middleware::content::packages
