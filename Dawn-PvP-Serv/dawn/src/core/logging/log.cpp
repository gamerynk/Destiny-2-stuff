#include "log.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../filesystem/path.h"
#include "snapshot/internal.h"

namespace dawn::core::log {
namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(Channel::count)> kChannelNames{
    "core", "client", "state", "server", "middleware"};
constexpr std::array<std::string_view, 4> kLevelNames{"error", "warn", "info", "debug"};
constexpr std::wstring_view kLogDirectorySuffix = L"\\logs";
constexpr std::wstring_view kClientLogFileSuffix = L"\\restoration-client.log";
constexpr std::wstring_view kServerLogFileSuffix = L"\\restoration-server.log";
constexpr std::wstring_view kPreviousLogSuffix = L".old";
constexpr std::string_view kLineEnding = "\r\n";
constexpr std::size_t kLineTerminatorBytes = 1;
constexpr std::size_t kElapsedCapacity = 128;
constexpr std::string_view kHexDigits = "0123456789ABCDEF";
constexpr std::size_t kHexDigitsPerByte = 2;
constexpr std::size_t kEventTextCapacity =
    kLineCapacity - kLineEnding.size() - kLineTerminatorBytes;
constexpr std::size_t kStampCapacity = 32;

struct LogState {
    SRWLOCK lock{SRWLOCK_INIT};
    std::array<std::atomic<Level>, static_cast<std::size_t>(Channel::count)> levels{};
    HANDLE file{INVALID_HANDLE_VALUE};
    ULONGLONG startTick{};
    bool debuggerSink{};
    bool initialized{};
};

LogState g_log;
std::atomic_int g_writers{};

[[nodiscard]] bool enabled(Channel channel, Level level) noexcept {
    const auto index = static_cast<std::size_t>(channel);
    if (index >= g_log.levels.size() || level == Level::off) {
        return false;
    }
    return static_cast<unsigned char>(level)
           <= static_cast<unsigned char>(g_log.levels[index].load(std::memory_order_relaxed));
}

[[nodiscard]] std::size_t append(std::array<char, kLineCapacity>& line,
                                 std::size_t offset,
                                 std::string_view text,
                                 std::size_t limit) noexcept {
    const std::size_t available = offset < limit ? limit - offset : 0;
    const std::size_t count = text.size() < available ? text.size() : available;
    if (count != 0) {
        std::memcpy(line.data() + offset, text.data(), count);
    }
    return offset + count;
}

[[nodiscard]] HANDLE open_log_file(void* module, ProcessRole role) noexcept {
    path::Buffer logPath;
    if (!path::artifact_directory(module, logPath) || !path::append(logPath, kLogDirectorySuffix)) {
        return INVALID_HANDLE_VALUE;
    }
    if (!CreateDirectoryW(logPath.chars.data(), nullptr)
        && GetLastError() != ERROR_ALREADY_EXISTS) {
        return INVALID_HANDLE_VALUE;
    }

    const std::wstring_view suffix = role == ProcessRole::server ? kServerLogFileSuffix
                                                                 : kClientLogFileSuffix;
    if (!path::append(logPath, suffix)) {
        return INVALID_HANDLE_VALUE;
    }
    path::Buffer oldPath = logPath;
    if (!path::append(oldPath, kPreviousLogSuffix)) {
        return INVALID_HANDLE_VALUE;
    }
    MoveFileExW(logPath.chars.data(), oldPath.chars.data(), MOVEFILE_REPLACE_EXISTING);

    return CreateFileW(logPath.chars.data(),
                       GENERIC_WRITE,
                       FILE_SHARE_READ,
                       nullptr,
                       CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL,
                       nullptr);
}

} // namespace

Settings defaults() noexcept {
    Settings settings;
    settings.levels.fill(Level::warn);
    return settings;
}

bool initialize(void* module, const Settings& settings, ProcessRole role) noexcept {
    AcquireSRWLockExclusive(&g_log.lock);
    // reset under the lifetime lock so an admitted writer can't repopulate a stale view
    snapshot::internal::reset();
    if (g_log.file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log.file);
        g_log.file = INVALID_HANDLE_VALUE;
    }
    g_log.initialized = false;
    g_log.debuggerSink = settings.debuggerSink;
    g_log.startTick = GetTickCount64();
    for (std::size_t index = 0; index < g_log.levels.size(); ++index) {
        g_log.levels[index].store(settings.levels[index], std::memory_order_relaxed);
    }
    if (settings.fileSink) {
        g_log.file = open_log_file(module, role);
    }
    const bool ready = !settings.fileSink || g_log.file != INVALID_HANDLE_VALUE;
    g_log.initialized = ready;
    if (!ready) {
        g_log.debuggerSink = false;
        for (std::atomic<Level>& level : g_log.levels) {
            level.store(Level::off, std::memory_order_relaxed);
        }
    }
    ReleaseSRWLockExclusive(&g_log.lock);
    return ready;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_log.lock);
    g_log.initialized = false;
    for (std::atomic<Level>& level : g_log.levels) {
        level.store(Level::off, std::memory_order_relaxed);
    }
    g_log.debuggerSink = false;
    if (g_log.file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_log.file);
        g_log.file = INVALID_HANDLE_VALUE;
    }
    snapshot::internal::reset();
    ReleaseSRWLockExclusive(&g_log.lock);
}

void early(std::string_view event) noexcept {
    std::array<char, kLineCapacity> line{};
    std::size_t length = append(line, 0, "core level=error ", kEventTextCapacity);
    length = append(line, length, event, kEventTextCapacity);
    std::memcpy(line.data() + length, kLineEnding.data(), kLineEnding.size());
    line[length + kLineEnding.size()] = '\0';
    g_writers.fetch_add(1, std::memory_order_acq_rel);
    OutputDebugStringA(line.data());
    g_writers.fetch_sub(1, std::memory_order_acq_rel);
}

bool accepts(Channel channel, Level level) noexcept {
    AcquireSRWLockShared(&g_log.lock);
    const bool admitted = g_log.initialized && enabled(channel, level);
    ReleaseSRWLockShared(&g_log.lock);
    return admitted;
}

void write(Channel channel, Level level, std::string_view event) noexcept {
    const auto channelIndex = static_cast<std::size_t>(channel);
    const auto levelIndex = static_cast<std::size_t>(level);
    if (channelIndex >= kChannelNames.size() || levelIndex >= kLevelNames.size()) {
        return;
    }

    AcquireSRWLockShared(&g_log.lock);
    if (!g_log.initialized || !enabled(channel, level)) {
        ReleaseSRWLockShared(&g_log.lock);
        return;
    }

    std::array<char, kStampCapacity> stamp{};
    const int stamped =
        std::snprintf(stamp.data(),
                      stamp.size(),
                      " t=%llu ",
                      static_cast<unsigned long long>(GetTickCount64() - g_log.startTick));

    std::array<char, kLineCapacity> line{};
    std::size_t length = append(line, 0, kChannelNames[channelIndex], kEventTextCapacity);
    length = append(line, length, " level=", kEventTextCapacity);
    length = append(line, length, kLevelNames[levelIndex], kEventTextCapacity);
    length = append(line,
                    length,
                    stamped > 0 ? std::string_view(stamp.data(), static_cast<std::size_t>(stamped))
                                : std::string_view(" "),
                    kEventTextCapacity);
    length = append(line, length, event, kEventTextCapacity);
    const std::size_t snapshotLength = length;
    std::memcpy(line.data() + length, kLineEnding.data(), kLineEnding.size());
    length += kLineEnding.size();
    line[length] = '\0';

    g_writers.fetch_add(1, std::memory_order_acq_rel);
    if (g_log.debuggerSink) {
        OutputDebugStringA(line.data());
    }
    if (g_log.file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_log.file, line.data(), static_cast<DWORD>(length), &written, nullptr);
    }
    g_writers.fetch_sub(1, std::memory_order_acq_rel);
    // record before releasing the shared lock so shutdown's reset can't race it
    snapshot::internal::record(channel, level, std::string_view(line.data(), snapshotLength));
    ReleaseSRWLockShared(&g_log.lock);
}

void writef(Channel channel, Level level, const char* format, ...) noexcept {
    if (format == nullptr || !accepts(channel, level)) {
        return;
    }
    std::array<char, kLineCapacity> line{};
    va_list arguments;
    va_start(arguments, format);
    const int count = std::vsnprintf(line.data(), line.size(), format, arguments);
    va_end(arguments);
    if (count <= 0) {
        return;
    }
    const std::size_t length = (std::min)(static_cast<std::size_t>(count), line.size() - 1);
    write(channel, level, std::string_view(line.data(), length));
}

void write_elapsed(Channel channel,
                   std::string_view event,
                   unsigned long long startedTick,
                   std::string_view result) noexcept {
    if (!accepts(channel, Level::debug)) {
        return;
    }
    const unsigned long long elapsed = GetTickCount64() - startedTick;
    std::array<char, kElapsedCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "%.*s ms=%llu result=%.*s",
                                      static_cast<int>(event.size()),
                                      event.data(),
                                      elapsed,
                                      static_cast<int>(result.size()),
                                      result.data());
    if (written <= 0) {
        return;
    }
    // snprintf returns the untruncated length, so clamp to the buffer
    const auto length =
        std::min(static_cast<std::size_t>(written), line.size() - kLineTerminatorBytes);
    write(channel, Level::debug, {line.data(), length});
}

bool append_hex(std::span<char> line,
                std::size_t& length,
                std::span<const std::byte> bytes) noexcept {
    for (const std::byte byte : bytes) {
        if (length + kHexDigitsPerByte >= line.size()) {
            return false;
        }
        const auto value = std::to_integer<unsigned>(byte);
        line[length++] = kHexDigits[(value >> 4U) & 0xFU];
        line[length++] = kHexDigits[value & 0xFU];
    }
    return true;
}

bool writers_active() noexcept {
    return g_writers.load(std::memory_order_acquire) != 0;
}

} // namespace dawn::core::log
