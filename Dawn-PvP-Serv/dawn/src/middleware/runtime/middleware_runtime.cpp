#include "middleware_runtime.h"

#include <Windows.h>

#include "../../core/filesystem/path.h"
#include "../../state/build_data/runtime.h"
#include "../content/packages/package_catalog.h"

namespace dawn::middleware {
namespace {

constexpr std::wstring_view kPackageDirectorySuffix = L"packages";

} // namespace

bool initialize() noexcept {
    if (state::build_data::named_catalog_ready()) {
        return true;
    }
    core::path::Buffer packageDirectory;
    if (!core::path::module_directory(GetModuleHandleW(nullptr), packageDirectory)
        || !core::path::append(packageDirectory, kPackageDirectorySuffix)) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(packageDirectory.chars.data());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return true;
    }
    content::packages::named_tags::DirectoryResult result{};
    return content::packages::load_catalog(packageDirectory.chars.data(), result)
           && state::build_data::publish_named_catalog();
}

void shutdown() noexcept {}

} // namespace dawn::middleware
