#include "hud_settings_store.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <string_view>

#include "../../../filesystem/path.h"
#include "../../../logging/log.h"

namespace dawn::core::ui::hud::store {
namespace {

constexpr std::wstring_view kFileSuffix = L"\\hud.json";
constexpr std::size_t kFileCapacity = 2048;
constexpr std::size_t kQuotedKeyCapacity = 48;
constexpr char kTrueText[] = "true";
constexpr char kFalseText[] = "false";

path::Buffer g_path{};
bool g_pathResolved{};

void report_fail(const char* reason) noexcept {
    std::array<char, 96> line{};
    const int written =
        std::snprintf(line.data(), line.size(), "ev=hud stage=store result=fail reason=%s", reason);
    if (written > 0) {
        log::write(
            log::Channel::core, log::Level::warn, {line.data(), static_cast<std::size_t>(written)});
    }
}

// the key is quoted here so a shorter key can't match inside a longer one
void switch_for(std::string_view text, const char* key, bool& output) noexcept {
    std::array<char, kQuotedKeyCapacity> quoted{};
    const int length = std::snprintf(quoted.data(), quoted.size(), "\"%s\"", key);
    if (length <= 0) {
        return;
    }
    const std::size_t at =
        text.find(std::string_view(quoted.data(), static_cast<std::size_t>(length)));
    if (at == std::string_view::npos) {
        return;
    }
    const std::size_t colon = text.find(':', at);
    if (colon == std::string_view::npos) {
        return;
    }
    std::size_t begin = colon + 1;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    const std::string_view value = text.substr(begin);
    if (value.starts_with(kTrueText)) {
        output = true;
    } else if (value.starts_with(kFalseText)) {
        output = false;
    }
}

[[nodiscard]] bool append_row(std::array<char, kFileCapacity>& document,
                              int& offset,
                              const Switch& entry,
                              bool last) noexcept {
    const auto used = static_cast<std::size_t>(offset);
    const int written = std::snprintf(document.data() + offset,
                                      document.size() - used,
                                      "  \"%s\": %s%s\n",
                                      entry.key,
                                      entry.on ? kTrueText : kFalseText,
                                      last ? "" : ",");
    if (written <= 0 || static_cast<std::size_t>(written) >= document.size() - used) {
        return false;
    }
    offset += written;
    return true;
}

} // namespace

void initialize(void* module) noexcept {
    g_path = path::Buffer{};
    g_pathResolved = path::artifact_directory(module, g_path) && path::append(g_path, kFileSuffix);
    if (!g_pathResolved) {
        report_fail("path");
    }
}

void shutdown() noexcept {
    g_path = path::Buffer{};
    g_pathResolved = false;
}

void load(std::span<Switch> switches) noexcept {
    if (!g_pathResolved) {
        return;
    }
    const HANDLE file = CreateFileW(g_path.chars.data(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    std::array<char, kFileCapacity> buffer{};
    DWORD read = 0;
    const bool readOk =
        ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, nullptr)
        != FALSE;
    (void)CloseHandle(file);
    if (!readOk || read == 0) {
        return;
    }
    const std::string_view text(buffer.data(), read);
    for (Switch& entry : switches) {
        switch_for(text, entry.key, entry.on);
    }
}

bool save(std::span<const Switch> switches) noexcept {
    if (!g_pathResolved) {
        return false;
    }
    std::array<char, kFileCapacity> document{};
    int offset = std::snprintf(document.data(), document.size(), "{\n");
    if (offset <= 0) {
        return false;
    }
    for (std::size_t index = 0; index < switches.size(); ++index) {
        if (!append_row(document, offset, switches[index], index + 1 == switches.size())) {
            report_fail("capacity");
            return false;
        }
    }
    const auto used = static_cast<std::size_t>(offset);
    const int closed = std::snprintf(document.data() + offset, document.size() - used, "}\n");
    if (closed <= 0 || static_cast<std::size_t>(closed) >= document.size() - used) {
        report_fail("capacity");
        return false;
    }
    offset += closed;

    const HANDLE file = CreateFileW(g_path.chars.data(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        report_fail("open");
        return false;
    }
    DWORD written = 0;
    const auto size = static_cast<DWORD>(offset);
    bool complete =
        WriteFile(file, document.data(), size, &written, nullptr) != FALSE && written == size;
    complete = CloseHandle(file) != FALSE && complete;
    if (!complete) {
        report_fail("write");
    }
    return complete;
}

} // namespace dawn::core::ui::hud::store
