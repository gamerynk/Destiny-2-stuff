#include "game_host_classification.h"

#include <Windows.h>

#include <array>
#include <string_view>

namespace dawn::client::runtime::host {
namespace {

constexpr std::wstring_view kGameExecutableName{L"destiny2.exe"};
constexpr std::wstring_view kPathSeparators{L"\\/"};
constexpr DWORD kMaximumExtendedPathCharacters = 32768;

} // namespace

NetworkRequirement classify(std::wstring_view executablePath) noexcept {
    if (executablePath.empty()) {
        return NetworkRequirement::required;
    }
    const std::size_t separator = executablePath.find_last_of(kPathSeparators);
    const std::wstring_view filename = separator == std::wstring_view::npos
                                           ? executablePath
                                           : executablePath.substr(separator + 1);
    if (filename.size() != kGameExecutableName.size()) {
        return NetworkRequirement::notApplicable;
    }
    const int comparison = CompareStringOrdinal(filename.data(),
                                                static_cast<int>(filename.size()),
                                                kGameExecutableName.data(),
                                                static_cast<int>(kGameExecutableName.size()),
                                                TRUE);
    if (comparison == CSTR_LESS_THAN || comparison == CSTR_GREATER_THAN) {
        return NetworkRequirement::notApplicable;
    }
    // equal and failed comparisons both keep game-network readiness fail-closed
    return NetworkRequirement::required;
}

NetworkRequirement current_requirement() noexcept {
    std::array<wchar_t, kMaximumExtendedPathCharacters> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        // unknown hosts are required so an identity lookup failure cannot bypass readiness
        return NetworkRequirement::required;
    }
    return classify(std::wstring_view(path.data(), length));
}

} // namespace dawn::client::runtime::host
