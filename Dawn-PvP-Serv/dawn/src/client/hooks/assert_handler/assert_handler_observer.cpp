#include "assert_handler_observer.h"

#include <Windows.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../../core/logging/log.h"
#include "../../targets/game/assert_handler.h"

namespace dawn::client::hooks::assert_handler {
namespace {

constexpr std::size_t kTextCapacity = 1024;
constexpr std::size_t kLineCapacity = 1152;
constexpr std::uint32_t kRepeatHead = 8;
constexpr std::uint32_t kRepeatStride = 512;
constexpr char kUnformattable[] = "<unformattable>";

// graphics device-loss halt category; a removed device never returns, so it must keep halting
constexpr int kGraphicsHaltCategory = 6;

using NativeHandler = void(__cdecl*)(int, const char*, ...);

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<char, kTextCapacity> g_lastText{};
std::uint32_t g_repeats{};
std::uint32_t g_seen{};

[[nodiscard]] bool unrecoverable(int code) noexcept {
    return code == kGraphicsHaltCategory;
}

[[nodiscard]] bool admit(const char* text, std::uint32_t& seen, std::uint32_t& repeats) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    ++g_seen;
    if (std::strcmp(g_lastText.data(), text) == 0) {
        ++g_repeats;
    } else {
        const std::size_t length = std::strlen(text);
        const std::size_t copied = length < kTextCapacity ? length : kTextCapacity - 1;
        std::memcpy(g_lastText.data(), text, copied);
        g_lastText[copied] = '\0';
        g_repeats = 1;
    }
    seen = g_seen;
    repeats = g_repeats;
    ReleaseSRWLockExclusive(&g_lock);
    return repeats <= kRepeatHead || repeats % kRepeatStride == 0;
}

void report(int code, const char* text) noexcept {
    std::uint32_t seen = 0;
    std::uint32_t repeats = 0;
    if (!admit(text, seen, repeats)) {
        return;
    }
    std::array<char, kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=assert stage=hit n=%u repeat=%u arg0=%d text=%s",
                                      seen,
                                      repeats,
                                      code,
                                      text);
    if (written <= 0) {
        return;
    }
    const auto length = static_cast<std::size_t>(written) < line.size()
                            ? static_cast<std::size_t>(written)
                            : line.size() - 1;
    core::log::write(core::log::Channel::client, core::log::Level::error, {line.data(), length});
}

void chain(int code, const char* text) noexcept {
    if (!targets::game::assert_handler::is_resolved()) {
        return;
    }
    const targets::game::assert_handler::Targets& resolved = targets::game::assert_handler::get();
    if (resolved.original == nullptr) {
        return;
    }
    std::array<char, 64> line{};
    const int written =
        std::snprintf(line.data(), line.size(), "ev=assert stage=halt arg0=%d result=native", code);
    if (written > 0) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::error,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    reinterpret_cast<NativeHandler>(resolved.original)(code, "%s", text);
}

// returning without calling the game's handler is what makes the assert non-fatal; that
// handler builds a crash ticket, shows a dialog and blocks
void __cdecl handler_body(int code, const char* format, ...) noexcept {
    std::array<char, kTextCapacity> text{};
    if (format == nullptr) {
        std::memcpy(text.data(), kUnformattable, sizeof kUnformattable);
    } else {
        va_list arguments;
        va_start(arguments, format);
        const int written = std::vsnprintf(text.data(), text.size(), format, arguments);
        va_end(arguments);
        if (written < 0) {
            std::memcpy(text.data(), kUnformattable, sizeof kUnformattable);
        }
    }
    text.back() = '\0';

    // an assert raised by our own logging must not re-enter this body
    static thread_local bool inside = false;
    if (inside) {
        return;
    }
    inside = true;
    report(code, text.data());
    // swallowing an unrecoverable halt leaves nothing to present, so let the original kill the process
    if (unrecoverable(code)) {
        chain(code, text.data());
    }
    inside = false;
}

} // namespace

void* handler_entry_point() noexcept {
    return reinterpret_cast<void*>(&handler_body);
}

} // namespace dawn::client::hooks::assert_handler
