#include <array>
#include <cstdio>

#include "../../../../core/logging/log.h"
#include "../internal.h"

namespace dawn::server::bap::encrypted::diagnostics {

// a reply-mode request left unanswered jams the client's pending ring for good, so nothing here may fail silently
void report_failure(std::uint16_t service, std::string_view stage) noexcept {
    std::array<char, core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=bap svc=%u stage=%.*s result=fail",
                                      static_cast<unsigned>(service),
                                      static_cast<int>(stage.size()),
                                      stage.data());
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::warn,
                         {line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace dawn::server::bap::encrypted::diagnostics
