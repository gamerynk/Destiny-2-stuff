#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "../../../resources/resource.h"
#include "../filesystem/path.h"
#include "../logging/log.h"
#include "settings.h"
#include "settings_upgrade.h"

namespace dawn::core::settings {
namespace {

constexpr std::wstring_view kSettingsFileSuffix = L"\\settings.json";
constexpr std::wstring_view kUpgradeStageSuffix = L".new";
constexpr std::size_t kConfigCapacity = 1024 * 1024;

Settings g_settings = defaults();

// settings load before the log sinks exist, so this line is the only boot failure report
[[nodiscard]] bool fail(std::string_view reason) noexcept {
    std::array<char, 96> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=settings result=fail reason=%.*s",
                                      static_cast<int>(reason.size()),
                                      reason.data());
    if (written > 0) {
        log::early({line.data(), static_cast<std::size_t>(written)});
    }
    return false;
}

void report_version(std::uint32_t fileVersion) noexcept {
    if (fileVersion == kSettingsVersion) {
        return;
    }
    std::array<char, 96> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=settings stage=version result=mismatch file=%u build=%u",
                                      static_cast<unsigned>(fileVersion),
                                      static_cast<unsigned>(kSettingsVersion));
    if (written > 0) {
        log::early({line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool bundled_document(void* module, std::string_view& output) noexcept {
    const HMODULE loadedModule = static_cast<HMODULE>(module);
    const HRSRC resource =
        FindResourceW(loadedModule, MAKEINTRESOURCEW(IDR_DEFAULT_SETTINGS), RT_RCDATA);
    if (resource == nullptr) {
        return false;
    }
    const DWORD size = SizeofResource(loadedModule, resource);
    const HGLOBAL loaded = LoadResource(loadedModule, resource);
    const auto* bytes =
        loaded != nullptr ? static_cast<const char*>(LockResource(loaded)) : nullptr;
    if (size == 0 || bytes == nullptr) {
        return false;
    }
    output = std::string_view(bytes, size);
    return true;
}

[[nodiscard]] bool write_default(void* module, const path::Buffer& configPath) noexcept {
    std::string_view document;
    if (!bundled_document(module, document)) {
        return false;
    }
    const HANDLE file = CreateFileW(configPath.chars.data(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const auto size = static_cast<DWORD>(document.size());
    bool complete =
        WriteFile(file, document.data(), size, &written, nullptr) != FALSE && written == size;
    complete = CloseHandle(file) != FALSE && complete;
    if (!complete) {
        // a half-written default must not become the next boot's settings
        (void)DeleteFileW(configPath.chars.data());
    }
    return complete;
}

// staged beside the file and moved over it so a failed write can't leave half a file
[[nodiscard]] bool store_upgraded(const path::Buffer& configPath,
                                  std::string_view document) noexcept {
    path::Buffer stagePath = configPath;
    if (!path::append(stagePath, kUpgradeStageSuffix)) {
        return false;
    }
    const HANDLE file = CreateFileW(stagePath.chars.data(),
                                    GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const auto size = static_cast<DWORD>(document.size());
    bool complete =
        WriteFile(file, document.data(), size, &written, nullptr) != FALSE && written == size;
    complete = CloseHandle(file) != FALSE && complete;
    complete =
        complete
        && MoveFileExW(stagePath.chars.data(), configPath.chars.data(), MOVEFILE_REPLACE_EXISTING)
               != FALSE;
    if (!complete) {
        (void)DeleteFileW(stagePath.chars.data());
    }
    return complete;
}

void report_upgrade(bool stored) noexcept {
    std::array<char, 96> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=settings stage=upgrade version=%u stored=%u",
                                      static_cast<unsigned>(kSettingsVersion),
                                      stored ? 1U : 0U);
    if (written > 0) {
        log::early({line.data(), static_cast<std::size_t>(written)});
    }
}

} // namespace

bool initialize(void* module) noexcept {
    path::Buffer configPath;
    if (!path::artifact_directory(module, configPath)
        || !path::append(configPath, kSettingsFileSuffix)) {
        return fail("path");
    }

    const HANDLE file = CreateFileW(configPath.chars.data(),
                                    GENERIC_READ,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    HANDLE readableFile = file;
    if (readableFile == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            return fail("open");
        }
        if (!write_default(module, configPath)) {
            return fail("write_default");
        }
        readableFile = CreateFileW(configPath.chars.data(),
                                   GENERIC_READ,
                                   FILE_SHARE_READ,
                                   nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
        if (readableFile == INVALID_HANDLE_VALUE) {
            return fail("reopen");
        }
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(readableFile, &size) || size.QuadPart <= 0) {
        CloseHandle(readableFile);
        return fail("empty");
    }
    if (static_cast<std::uint64_t>(size.QuadPart) > kConfigCapacity) {
        // silence here looks exactly like a crash, and the cap is the usual cause
        CloseHandle(readableFile);
        return fail("too_large");
    }

    // static because two 1 MiB banks overflow the stack; settings load once on one thread
    static std::array<char, kConfigCapacity> buffer{};
    DWORD read = 0;
    const bool readOk =
        ReadFile(readableFile, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr)
            != FALSE
        && read == size.QuadPart;
    const bool closed = CloseHandle(readableFile) != FALSE;
    if (!readOk || !closed) {
        return fail("read");
    }
    std::string_view document(buffer.data(), read);
    static std::array<char, kConfigCapacity> upgradedBuffer{};
    const bool upgrading = upgrade::needed(document);
    if (upgrading) {
        std::string_view bundled;
        std::size_t upgraded = 0;
        if (!bundled_document(module, bundled)
            || !upgrade::apply(document, bundled, upgradedBuffer, upgraded)) {
            return fail("upgrade");
        }
        document = std::string_view(upgradedBuffer.data(), upgraded);
    }

    Settings parsed;
    if (!parse(document, parsed)) {
        return fail("parse");
    }
    if (upgrading) {
        report_upgrade(store_upgraded(configPath, document));
    }
    report_version(parsed.version);
    g_settings = parsed;
    return true;
}

void shutdown() noexcept {
    g_settings = defaults();
}

const Settings& get() noexcept {
    return g_settings;
}

} // namespace dawn::core::settings
