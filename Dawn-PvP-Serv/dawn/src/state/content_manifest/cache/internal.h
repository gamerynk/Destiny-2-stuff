#pragma once

#include <cstddef>
#include <span>

#include "../definition.h"

namespace dawn::state::content_manifest::cache {

enum class LoadStatus : std::uint8_t {
    missing,
    stale,
    invalid,
    loaded,
};

[[nodiscard]] LoadStatus load(const wchar_t* path,
                              const Fingerprint& directoryFingerprint,
                              std::span<Row> rows,
                              std::size_t& count,
                              Fingerprint& buildFingerprint,
                              Guid& guid,
                              bool offlineSnapshot = false) noexcept;

[[nodiscard]] bool write(const wchar_t* directory,
                         const wchar_t* path,
                         const Fingerprint& directoryFingerprint,
                         const Fingerprint& buildFingerprint,
                         std::span<const Row> rows,
                         std::span<Row> validationRows) noexcept;

} // namespace dawn::state::content_manifest::cache
