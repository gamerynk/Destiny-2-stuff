#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace dawn::core::log {

inline constexpr std::size_t kLineCapacity = 1024;

enum class Channel : std::size_t {
    core,
    client,
    state,
    server,
    middleware,
    count,
};

enum class Level : unsigned char {
    error,
    warn,
    info,
    debug,
    off,
};

enum class ProcessRole : unsigned char {
    client,
    server,
};

struct Settings {
    std::array<Level, static_cast<std::size_t>(Channel::count)> levels{};
    bool debuggerSink{true};
    bool fileSink{false};
};

[[nodiscard]] Settings defaults() noexcept;

[[nodiscard]] bool initialize(void* module,
                              const Settings& settings,
                              ProcessRole role = ProcessRole::client) noexcept;

void shutdown() noexcept;

[[nodiscard]] bool accepts(Channel channel, Level level) noexcept;

void write(Channel channel, Level level, std::string_view event) noexcept;

void writef(Channel channel, Level level, const char* format, ...) noexcept;

void write_elapsed(Channel channel,
                   std::string_view event,
                   unsigned long long startedTick,
                   std::string_view result) noexcept;

bool append_hex(std::span<char> line,
                std::size_t& length,
                std::span<const std::byte> bytes) noexcept;

// a suspended thread inside a sink blocks the next writer; never suspend process threads
// while this is true
[[nodiscard]] bool writers_active() noexcept;

// settings load before the sinks exist, so a boot failure has no other channel
void early(std::string_view event) noexcept;

} // namespace dawn::core::log
