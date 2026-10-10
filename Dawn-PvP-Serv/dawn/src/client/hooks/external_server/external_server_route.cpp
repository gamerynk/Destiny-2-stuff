// URL host rewrite for external-server mode; the Client resolves its SignOn hosts outside
// the hooked resolver entries, so it needs a rewritten URL rather than a redirected lookup

#include <algorithm>

#include "../../../core/settings/settings.h"
#include "route.h"

namespace dawn::client::hooks::external_server {
namespace {

constexpr std::string_view kSchemeSeparator = "://";

} // namespace

bool enabled() noexcept {
    return core::settings::get().client.externalServer.enabled;
}

std::size_t rewrite_host(std::string_view url, std::span<char> output) noexcept {
    const std::size_t scheme = url.find(kSchemeSeparator);
    if (scheme == std::string_view::npos || output.empty()) {
        return 0;
    }
    const std::size_t hostStart = scheme + kSchemeSeparator.size();
    const std::size_t pathStart = url.find_first_of("/?#", hostStart);
    const std::string_view suffix =
        pathStart == std::string_view::npos ? std::string_view{} : url.substr(pathStart);
    const std::string_view origin(core::settings::get().client.externalServer.origin.data());
    const bool emptyPath = !suffix.empty() && suffix.front() != '/';
    const std::size_t size = origin.size() + suffix.size() + (emptyPath ? 1U : 0U);
    if (origin.empty() || size + 1 > output.size()) {
        return 0;
    }

    auto next = std::copy(origin.begin(), origin.end(), output.begin());
    if (emptyPath) {
        *next++ = '/';
    }
    next = std::copy(suffix.begin(), suffix.end(), next);
    *next = '\0';
    return size;
}

} // namespace dawn::client::hooks::external_server
