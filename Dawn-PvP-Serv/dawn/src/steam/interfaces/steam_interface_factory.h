#pragma once

namespace dawn::steam::interfaces {

[[nodiscard]] void* create(const char* version) noexcept;

[[nodiscard]] void* find_user(const char* version) noexcept;

[[nodiscard]] void* find_global(const char* version) noexcept;

} // namespace dawn::steam::interfaces
