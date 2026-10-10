#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../../../state/content_manifest/definition.h"
#include "../../../state/entitlements/definition.h"

namespace dawn::middleware::content::manifest {

[[nodiscard]] bool encode_entitlement(const state::entitlements::Entitlement& entry,
                                      std::uint32_t definitionHandle,
                                      std::span<std::byte> output,
                                      std::size_t& size) noexcept;

[[nodiscard]] bool encode(const state::entitlements::Table& entitlements,
                          std::span<const state::content_manifest::Row> rows,
                          std::string_view guid,
                          std::span<std::byte> output,
                          std::size_t& size) noexcept;

} // namespace dawn::middleware::content::manifest
