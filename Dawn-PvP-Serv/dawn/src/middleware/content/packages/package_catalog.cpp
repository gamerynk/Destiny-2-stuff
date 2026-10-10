#include "package_catalog.h"

#include "../../../state/content/content_catalog.h"

namespace dawn::middleware::content::packages {
namespace {

[[nodiscard]] bool insert_mapping([[maybe_unused]] void* context,
                                  const named_tags::Entry& entry) noexcept {
    return state::content::insert(
        std::string_view(entry.name.data(), entry.nameLength), entry.tag, entry.classId);
}

} // namespace

bool load_catalog(std::wstring_view directory, named_tags::DirectoryResult& result) noexcept {
    state::content::clear();
    if (named_tags::extract_directory(directory, &insert_mapping, nullptr, result)) {
        return true;
    }
    state::content::clear();
    result = {};
    return false;
}

} // namespace dawn::middleware::content::packages
