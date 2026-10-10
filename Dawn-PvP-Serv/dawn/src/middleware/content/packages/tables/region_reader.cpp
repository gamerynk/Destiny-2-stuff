#include "region_reader.h"

#include <cstdio>

#include "internal.h"

namespace dawn::middleware::content::packages::tables {
namespace {

constexpr const char* kPublicPrefix = "PUB";
constexpr const char* kPrivatePrefix = "PRV";
// prefix, then region index twice split by a dot, 2 digits each
constexpr const char* kNameFormat = "%s%02u.%02u";

} // namespace

bool region_name(std::uint32_t regionIndex, bool publicBubble, RegionName& output) noexcept {
    output = {};
    if (regionIndex >= kRegionIndexBound) {
        return false;
    }
    const char* const prefix = publicBubble ? kPublicPrefix : kPrivatePrefix;
    const int written = std::snprintf(
        output.chars.data(), output.chars.size(), kNameFormat, prefix, regionIndex, regionIndex);
    if (written > 0 && static_cast<std::size_t>(written) < output.chars.size()) {
        output.length = static_cast<std::size_t>(written);
        return true;
    }
    output = {};
    return false;
}

} // namespace dawn::middleware::content::packages::tables
