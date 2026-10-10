#include "content_configuration.h"
#include <Windows.h>
#include <algorithm>
#include <map>
#include <new>
#include <utility>
#include "../../core/settings/settings.h"
#include "../../state/content_manifest/content_manifest_state_runtime.h"

namespace dawn::server::http::content_configuration {
namespace {
using Url = std::array<char, middleware::content::bootstrap_configuration::kUrlCapacity>;
SRWLOCK g_lock{SRWLOCK_INIT};
std::map<std::pair<std::uint64_t, std::uint64_t>, Url> g_entries;
bool copy_guid(void* context, const state::content_manifest::View& view) noexcept {
    if (view.guid.size() != 36) return false;
    auto& output = *static_cast<Snapshot*>(context);
    std::copy(view.guid.begin(), view.guid.end(), output.guid.begin());
    output.guid.back() = 0;
    return true;
}
}
bool publish(std::uint64_t device, std::uint64_t platform, std::string_view url) noexcept {
    if (platform == 0 || !middleware::content::bootstrap_configuration::valid_url(url)) return false;
    Url candidate{};
    std::copy(url.begin(), url.end(), candidate.begin());
    AcquireSRWLockExclusive(&g_lock);
    bool published = false;
    try {
        g_entries.insert_or_assign({device, platform}, candidate);
        published = true;
    } catch (const std::bad_alloc&) {}
    ReleaseSRWLockExclusive(&g_lock);
    return published;
}
bool snapshot(std::uint64_t device, std::uint64_t platform, Snapshot& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    const auto entry = g_entries.find({device, platform});
    if (platform != 0 && entry != g_entries.end()) output.url = entry->second;
    ReleaseSRWLockShared(&g_lock);
    if (output.url[0] == 0) {
        const auto& fallback = core::settings::get().client.externalServer.configUrl;
        if (!middleware::content::bootstrap_configuration::valid_url(fallback.data())) return false;
        output.url = fallback;
    }
    return state::content_manifest::visit_snapshot(&copy_guid, &output);
}
void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_entries.clear();
    ReleaseSRWLockExclusive(&g_lock);
}
}
