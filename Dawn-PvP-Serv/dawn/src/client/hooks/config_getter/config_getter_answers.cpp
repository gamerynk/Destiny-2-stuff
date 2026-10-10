#include "config_getter_answers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>

#include "../../../core/settings/settings.h"
#include "../../../state/content_manifest/content_manifest_state_runtime.h"
#include "../network/content_config/protocol.h"

namespace dawn::client::hooks::config_getter {
namespace {

// the Client copies the token as a fixed 37-byte field
constexpr std::size_t kTokenSize = state::content_manifest::kGuidCapacity;
static_assert(kTokenSize == 37);
constexpr char kSupplied = 1;
constexpr std::array<std::size_t, 4> kGuidHyphens{8, 13, 18, 23};

std::array<char, kTokenSize> g_token{};
std::atomic<bool> g_tokenReady{};

[[nodiscard]] bool valid_guid(std::string_view guid) noexcept {
    if (guid.size() != kTokenSize - 1) {
        return false;
    }
    for (std::size_t index = 0; index < guid.size(); ++index) {
        const bool hyphen = std::find(kGuidHyphens.begin(), kGuidHyphens.end(), index)
                            != kGuidHyphens.end();
        const char value = guid[index];
        const bool lowerHex = (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        if ((hyphen && value != '-') || (!hyphen && !lowerHex)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool copy_guid(void* context, const state::content_manifest::View& view) noexcept {
    auto* output = static_cast<std::array<char, kTokenSize>*>(context);
    if (view.guid.size() != kTokenSize - 1) {
        return false;
    }
    std::copy(view.guid.begin(), view.guid.end(), output->begin());
    output->back() = '\0';
    return true;
}

[[nodiscard]] bool ensure_token() noexcept {
    if (g_tokenReady.load(std::memory_order_acquire)) {
        return true;
    }
    const core::settings::client::external::Settings& external =
        core::settings::get().client.externalServer;
    if (external.enabled) {
        return false;
    } else if (!state::content_manifest::visit_snapshot(&copy_guid, &g_token)) {
        return false;
    }
    g_tokenReady.store(true, std::memory_order_release);
    return true;
}

char __fastcall url_body([[maybe_unused]] void* self, char* output) noexcept {
    if (output == nullptr) {
        return 0;
    }
    const core::settings::client::external::Settings& external =
        core::settings::get().client.externalServer;
    const std::string_view url = external.enabled
                                     ? std::string_view(external.configUrl.data())
                                     : ::dawn::client::hooks::network::content_config::kLocalUrl;
    std::copy(url.begin(), url.end(), output);
    output[url.size()] = '\0';
    return kSupplied;
}

// the Client copies 37 bytes and later compares them to the manifest identity field
void* __fastcall token_body([[maybe_unused]] void* self) noexcept {
    return ensure_token() ? static_cast<void*>(g_token.data()) : nullptr;
}

} // namespace

bool publish_server_guid(std::string_view guid) noexcept {
    if (!valid_guid(guid) || g_tokenReady.load(std::memory_order_acquire)) {
        return false;
    }
    std::copy(guid.begin(), guid.end(), g_token.begin());
    g_token.back() = '\0';
    g_tokenReady.store(true, std::memory_order_release);
    return true;
}

void* url_entry_point() noexcept {
    return reinterpret_cast<void*>(&url_body);
}

void* token_entry_point() noexcept {
    return reinterpret_cast<void*>(&token_body);
}

} // namespace dawn::client::hooks::config_getter
