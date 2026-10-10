#include "gameplay_log.h"

#include <array>
#include <cstdarg>
#include <cstdio>

namespace dawn::server::gameplay {

void report(core::log::Level level, const char* format, ...) noexcept {
    if (!core::log::accepts(core::log::Channel::server, level)) {
        return;
    }
    std::array<char, core::log::kLineCapacity> line{};
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(line.data(), line.size(), format, arguments);
    va_end(arguments);
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::server, level, {line.data(), length});
}

} // namespace dawn::server::gameplay
