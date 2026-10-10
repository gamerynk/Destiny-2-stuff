#include "internal.h"

namespace dawn::middleware::content::packages::named_tags {
namespace {

constexpr std::wstring_view kExtension = L".pkg";

} // namespace

std::uint32_t leaf_patch_index(const wchar_t* fileName) noexcept {
    if (fileName == nullptr) {
        return 0;
    }
    const std::wstring_view leaf{fileName};
    if (leaf.size() <= kExtension.size() || !leaf.ends_with(kExtension)) {
        return 0;
    }
    const std::wstring_view stem = leaf.substr(0, leaf.size() - kExtension.size());
    const std::size_t separator = stem.rfind(L'_');
    if (separator == std::wstring_view::npos || separator + 1 >= stem.size()) {
        return 0;
    }
    std::uint32_t patchIndex = 0;
    for (const wchar_t value : stem.substr(separator + 1)) {
        if (value < L'0' || value > L'9') {
            return 0;
        }
        patchIndex = patchIndex * 10U + static_cast<std::uint32_t>(value - L'0');
    }
    return patchIndex;
}

bool stamp_patch(void* context, const Entry& entry) noexcept {
    auto& stamp = *static_cast<PatchStamp*>(context);
    if (stamp.inner == nullptr) {
        return false;
    }
    Entry stamped = entry;
    stamped.patchIndex = stamp.patchIndex;
    return stamp.inner(stamp.context, stamped);
}

} // namespace dawn::middleware::content::packages::named_tags
