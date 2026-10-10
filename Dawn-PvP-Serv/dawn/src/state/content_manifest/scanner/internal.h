#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "../definition.h"
#include "package/content_manifest_package_name.h"

namespace dawn::state::content_manifest::scanner {

struct Candidate final {
    package::ParsedName parsed;
    std::uint64_t fileSize{};
    std::uint64_t lastWriteTime{};
    std::uint64_t buildSignature{};
};

[[nodiscard]] bool inventory(std::wstring_view directory,
                             std::span<Candidate> candidates,
                             std::size_t& count,
                             Fingerprint& fingerprint) noexcept;

[[nodiscard]] bool extract(std::wstring_view directory,
                           std::span<Candidate> candidates,
                           const Fingerprint& directoryFingerprint,
                           std::span<Row> rows,
                           std::size_t& count,
                           Fingerprint& buildFingerprint,
                           Guid& guid) noexcept;

} // namespace dawn::state::content_manifest::scanner
