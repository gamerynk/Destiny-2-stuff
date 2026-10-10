#include <array>
#include <atomic>
#include <cstdio>

#include "../../../../../core/logging/log.h"
#include "../../../../../state/runtime/runtime.h"
#include "../../internal.h"

namespace dawn::server::bap::encrypted::push {
namespace {

// set once the answer can no longer change, so the common path is one relaxed load instead of a lock and account copy per frame
std::atomic<bool> g_settled{false};

void report(const char* reason) noexcept {
    std::array<char, 96> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=queuez stage=account_preflight result=skip reason=%s",
                                      reason);
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

void ensure_account_canonical() noexcept {
    if (g_settled.load(std::memory_order_acquire)) {
        return;
    }
    switch (state::ensure_character_emote_collection()) {
    case state::EmoteCollectionOutcome::ready:
        g_settled.store(true, std::memory_order_release);
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         "ev=queuez stage=account_preflight result=ok");
        break;
    case state::EmoteCollectionOutcome::unsupported:
        g_settled.store(true, std::memory_order_release);
        report("unsupported");
        break;
    case state::EmoteCollectionOutcome::notReady:
        report("not_ready");
        break;
    case state::EmoteCollectionOutcome::failed:
        report("failed");
        break;
    }
}

} // namespace dawn::server::bap::encrypted::push
