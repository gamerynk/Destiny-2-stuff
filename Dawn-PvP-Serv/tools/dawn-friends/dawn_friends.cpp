#include <Windows.h>
#include <winhttp.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "device_identity.h"

namespace identity = dawn::core::identity::device_identity;

std::string utf8(const wchar_t* value) {
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                                              nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
                            result.data(), required, nullptr, nullptr) != required) return {};
    result.pop_back();
    return result;
}

std::uint64_t active_platform_id() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return 0;
    DWORD accountId = 0;
    DWORD type = 0;
    DWORD size = sizeof(accountId);
    const LONG result = RegQueryValueExW(key, L"ActiveUser", nullptr, &type,
                                          reinterpret_cast<BYTE*>(&accountId), &size);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(accountId)
        || accountId == 0) return 0;
    return 0x0110000100000000ULL | accountId;
}

bool existing_dawn_identity() {
    std::array<wchar_t, 2048> path{};
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                            static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return false;
    std::wstring file(path.data(), length);
    const auto separator = file.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return false;
    file.resize(separator + 1);
    file += L"Restoration\\device_identity.key";
    const DWORD attributes = GetFileAttributesW(file.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

int wmain(int argc, wchar_t** argv) {
    const wchar_t* server = L"127.0.0.1";
    INTERNET_PORT port = 30977;
    if (argc >= 5 && std::wcscmp(argv[argc - 3], L"--server") == 0) {
        server = argv[argc - 2];
        wchar_t* end{};
        const auto parsed = std::wcstoul(argv[argc - 1], &end, 10);
        if (!*server || !end || *end || !parsed || parsed > 65535) {
            std::puts("Invalid server host or port.");
            return 2;
        }
        port = static_cast<INTERNET_PORT>(parsed);
        argc -= 3;
    }
    const bool whoami = argc == 2 && std::wcscmp(argv[1], L"whoami") == 0;
    const bool add = argc == 3 && std::wcscmp(argv[1], L"add") == 0;
    const bool accept = argc == 3 && std::wcscmp(argv[1], L"accept") == 0;
    const bool rename = argc == 3 && std::wcscmp(argv[1], L"rename") == 0;
    if (!whoami && !add && !accept && !rename) {
        std::puts("Usage: DawnFriends.exe whoami | add NAME | accept NAME | rename NAME [--server HOST PORT]");
        return 2;
    }
    const std::string name = whoami ? std::string{} : utf8(argv[2]);
    if (!whoami && (name.empty() || name.size() >= 64
                    || name.find_first_of("\r\n") != std::string::npos)) {
        std::puts("Enter the exact Dawn account name (up to 63 UTF-8 bytes).");
        return 2;
    }
    if (rename && (name.size() < 3 || name.front() == ' ' || name.back() == ' '
        || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-")
            != std::string::npos)) {
        std::puts("Names must be 3-63 letters, numbers, spaces, _ or -.");
        return 2;
    }
    const std::uint64_t platformId = active_platform_id();
    if (!platformId) {
        std::puts("Could not read the active Steam account. Open Steam and sign in.");
        return 1;
    }
    if (!existing_dawn_identity()) {
        std::puts("No Dawn identity found. Run this from the game's bin\\x64 folder after signing in to Dawn.");
        return 1;
    }
    if (!identity::initialize(GetModuleHandleW(nullptr), platformId)) {
        std::puts("Could not read this game's Dawn identity. Run beside bin\\x64\\Restoration.");
        return 1;
    }
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::array<std::byte, identity::kProofSize> proof{};
    if (now <= 0 || !identity::issue(rename ? identity::Scope::accountRename
                                          : whoami ? identity::Scope::discordLinkCode
                                                   : identity::Scope::socialRelationships,
                                    static_cast<std::uint64_t>(now), proof)) {
        std::puts("Could not sign a Dawn account request.");
        return 1;
    }
    std::vector<unsigned char> body;
    body.reserve(proof.size() + 2 + name.size());
    for (const std::byte value : proof) body.push_back(std::to_integer<unsigned char>(value));
    if (!whoami) {
        if (!rename) body.push_back(add ? 1 : 2);
        body.push_back(static_cast<unsigned char>(name.size()));
        body.insert(body.end(), name.begin(), name.end());
    }

    HINTERNET session = WinHttpOpen(L"DawnFriends/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = session ? WinHttpConnect(session, server, port, 0) : nullptr;
    HINTERNET request = connection
        ? WinHttpOpenRequest(connection, L"POST", rename ? L"/v1/account/name"
                                                       : whoami ? L"/v1/account/discord-link-state"
                                                                : L"/v1/social/relationships", nullptr,
                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
        : nullptr;
    if (request) WinHttpSetTimeouts(request, 5000, 5000, 5000, 5000);
    const bool sent = request
        && WinHttpSendRequest(request, L"Content-Type: application/octet-stream\r\n",
                              static_cast<DWORD>(-1),
                              body.data(), static_cast<DWORD>(body.size()),
                              static_cast<DWORD>(body.size()), 0)
        && WinHttpReceiveResponse(request, nullptr);
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (sent) WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                  WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                  WINHTTP_NO_HEADER_INDEX);
    std::string reply;
    if (sent) {
        std::array<char, 1024> chunk{};
        DWORD count = 0;
        while (reply.size() < 4096 && WinHttpReadData(request, chunk.data(),
                                                       static_cast<DWORD>(chunk.size()), &count)
               && count != 0) reply.append(chunk.data(), count);
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    if (!sent) {
        std::puts("Could not reach the configured Dawn server.");
        return 1;
    }
    if (rename && (status == 404 || status == 403)) {
        std::puts("This server does not support Change Name yet. Your name was not changed.");
    } else {
        std::printf("Dawn server response (%lu): %s\n", status, reply.c_str());
    }
    return status == 200 ? 0 : 1;
}
