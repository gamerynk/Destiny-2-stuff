#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "../internal.h"

namespace dawn::steam::interfaces::methods {
namespace {

constexpr std::string_view kBetaName = "public";
// no launch parameters; must be an empty string, never null
constexpr char kEmptyQueryParam[] = "";
constexpr int kBuildId = 10;
constexpr DWORD kPathCapacity = 260;

[[nodiscard]] std::uint32_t executable_directory(char* output, std::uint32_t capacity) noexcept {
    char path[kPathCapacity]{};
    const DWORD length = GetModuleFileNameA(nullptr, path, kPathCapacity);
    if (length == 0 || length >= kPathCapacity) {
        return 0;
    }
    std::string_view view(path, length);
    const std::size_t separator = view.find_last_of("\\/");
    if (separator == std::string_view::npos) {
        return 0;
    }
    view = view.substr(0, separator);
    if (output == nullptr || capacity <= view.size()) {
        return 0;
    }
    std::copy(view.begin(), view.end(), output);
    output[view.size()] = '\0';
    return static_cast<std::uint32_t>(view.size());
}

} // namespace

bool current_beta_name([[maybe_unused]] void* self, char* name, int capacity) noexcept {
    if (name == nullptr || capacity <= static_cast<int>(kBetaName.size())) {
        return false;
    }
    std::copy(kBetaName.begin(), kBetaName.end(), name);
    name[kBetaName.size()] = '\0';
    return true;
}

std::uint32_t app_install_dir([[maybe_unused]] void* self,
                              [[maybe_unused]] DWORD appId,
                              char* folder,
                              std::uint32_t capacity) noexcept {
    return executable_directory(folder, capacity);
}

const char* launch_query_param([[maybe_unused]] void* self,
                               [[maybe_unused]] const char* key) noexcept {
    return kEmptyQueryParam;
}

int app_build_id([[maybe_unused]] void* self) noexcept {
    return kBuildId;
}

} // namespace dawn::steam::interfaces::methods
