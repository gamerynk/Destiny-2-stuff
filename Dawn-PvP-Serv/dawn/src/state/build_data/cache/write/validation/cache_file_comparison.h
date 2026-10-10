#pragma once

namespace dawn::state::build_data::cache::validation {

[[nodiscard]] bool files_equal(const wchar_t* firstPath, const wchar_t* secondPath) noexcept;

} // namespace dawn::state::build_data::cache::validation
