#include "runtime_policy.h"

#include <array>
#include <cstdio>

#include "../logging/log.h"

namespace dawn::core::runtime::policy {
namespace {

void write_record(const char* format,
                  std::string_view id,
                  bool enabled,
                  bool mutation,
                  std::string_view classification) noexcept {
    std::array<char, log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      format,
                                      static_cast<int>(id.size()),
                                      id.data(),
                                      enabled ? 1U : 0U,
                                      mutation ? 1U : 0U,
                                      static_cast<int>(classification.size()),
                                      classification.data());
    if (written <= 0) {
        return;
    }
    const std::size_t length =
        static_cast<std::size_t>(written) < line.size()
            ? static_cast<std::size_t>(written)
            : line.size() - 1;
    log::write(log::Channel::core, log::Level::info, {line.data(), length});
}

} // namespace

void report() noexcept {
    log::write(log::Channel::core,
               log::Level::info,
               kDedicatedBackendRequired
                   ? "ev=runtime_policy policy=dedicated_backend_required enabled=1"
                   : "ev=runtime_policy policy=dedicated_backend_required enabled=0");
    for (const StateWriter& writer : kActivityStateWriters) {
        write_record("ev=runtime_policy kind=state_writer id=%.*s enabled=%u mutation=%u ownership=%.*s",
                     writer.id,
                     writer.enabled,
                     writer.enabled,
                     writer.ownership);
    }
    for (const ClientHook& hook : kClientHooks) {
        write_record("ev=runtime_policy kind=client_hook id=%.*s enabled=%u mutation=%u role=%.*s",
                     hook.id,
                     hook.configuredToInstall,
                     hook.mutation && hook.configuredToInstall,
                     hook.role);
    }
}

} // namespace dawn::core::runtime::policy
