#include "content_manifest_row_validation.h"

#include <algorithm>
#include <string_view>

#include "scanner/package/content_manifest_package_name.h"

namespace dawn::state::content_manifest {
namespace {

[[nodiscard]] bool row_less(const Row& first, const Row& second) noexcept {
    return std::string_view(first.name.data(), first.nameLength)
           < std::string_view(second.name.data(), second.nameLength);
}

} // namespace

bool valid(const Row& row) noexcept {
    if (row.nameLength == 0 || row.nameLength >= row.name.size()) {
        return false;
    }
    const auto usedEnd = row.name.begin() + row.nameLength;
    if (std::find(row.name.begin(), usedEnd, '\0') != usedEnd
        || !std::all_of(usedEnd, row.name.end(), [](char value) { return value == '\0'; })) {
        return false;
    }
    scanner::package::ParsedName parsed{};
    return scanner::package::parse_stem(std::string_view(row.name.data(), row.nameLength), parsed)
           && parsed.packageId == row.packageId;
}

// package ids can repeat: each patch file of an id gets its own row
bool valid(std::span<const Row> rows) noexcept {
    if (rows.empty() || rows.size() > kRowCapacity) {
        return false;
    }
    for (std::size_t index = 0; index < rows.size(); ++index) {
        // strict ordering also proves names are unique
        if (!valid(rows[index]) || (index != 0 && !row_less(rows[index - 1], rows[index]))) {
            return false;
        }
    }
    return true;
}

} // namespace dawn::state::content_manifest
