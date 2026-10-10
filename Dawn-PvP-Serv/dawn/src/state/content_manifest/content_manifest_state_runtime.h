#pragma once

#include <string_view>

#include "definition.h"

namespace dawn::state::content_manifest {

[[nodiscard]] bool initialize(void* module,
                              std::wstring_view packagesDirectory,
                              bool allowOfflineSnapshot = false) noexcept;

void shutdown() noexcept;

[[nodiscard]] bool visit_snapshot(SnapshotVisitor visitor, void* context) noexcept;

} // namespace dawn::state::content_manifest
