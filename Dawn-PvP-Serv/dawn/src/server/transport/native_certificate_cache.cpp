#include "native_certificate_cache.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace dawn::server::transport::native_certificate_cache {
namespace {
namespace wire = middleware::bap::native_certificate;
struct Entry final {
    wire::Metadata metadata{};
    Snapshot certificate{};
};
std::vector<Entry> g_entries;
std::wstring g_directory;
SRWLOCK g_lock{SRWLOCK_INIT};
std::uint64_t g_temporarySequence{};
bool directory_exists(const wchar_t* path) noexcept {
    const auto attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
std::wstring filename(std::uint64_t platform, const std::array<std::byte, 32>& key) {
    wchar_t identity[17]{};
    (void)swprintf_s(identity, L"%016llX", static_cast<unsigned long long>(platform));
    std::wstring path = g_directory + L"\\" + identity + L"-";
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (const auto byte : key) {
        const auto value = std::to_integer<unsigned>(byte);
        path.push_back(hex[value >> 4]); path.push_back(hex[value & 15]);
    }
    return path + L".cert";
}
// every disk load revalidates identity, exact key, app, public-only shape and expiry; the native
// Steam parser still verifies the CA signature, disk presence never proves native acceptance
bool load(const std::wstring& path, std::uint64_t platform,
          const std::array<std::byte, 32>& key, std::uint64_t now, Entry& output) noexcept {
    output = {};
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    BY_HANDLE_FILE_INFORMATION information{};
    DWORD received{};
    const bool read = GetFileInformationByHandle(file, &information) != FALSE
        && (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0
        && GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0
        && size.QuadPart <= static_cast<LONGLONG>(output.certificate.bytes.size())
        && ReadFile(file, output.certificate.bytes.data(), static_cast<DWORD>(size.QuadPart), &received, nullptr) != FALSE
        && received == size.QuadPart;
    CloseHandle(file);
    if (!read) return false;
    output.certificate.size = received;
    return wire::signed_metadata(std::span(output.certificate.bytes).first(received), platform, now, output.metadata)
        && output.metadata.key == key;
}
bool store(const std::wstring& path, const Snapshot& certificate) {
    wchar_t suffix[96]{};
    (void)swprintf_s(suffix, L".tmp-%lu-%lu-%llu-%llu", GetCurrentProcessId(), GetCurrentThreadId(),
        static_cast<unsigned long long>(GetTickCount64()),
        static_cast<unsigned long long>(++g_temporarySequence));
    const std::wstring temporary = path + suffix;
    const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written{};
    const bool complete = WriteFile(file, certificate.bytes.data(), static_cast<DWORD>(certificate.size), &written, nullptr) != FALSE
        && written == certificate.size && FlushFileBuffers(file) != FALSE;
    const bool closed = CloseHandle(file) != FALSE;
    const bool replaced = complete && closed
        && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!replaced) (void)DeleteFileW(temporary.c_str());
    return replaced;
}
}
bool configure_storage(std::wstring_view directory) noexcept {
    if (!directory.empty()) {
        const bool absolute = (directory.size() >= 3 && directory[1] == L':'
            && (directory[2] == L'\\' || directory[2] == L'/'))
            || (directory.size() >= 3 && directory[0] == L'\\' && directory[1] == L'\\');
        if (!absolute || directory.find(L'\0') != std::wstring_view::npos) return false;
    }
    try {
        std::wstring value(directory);
        if (!value.empty() && !directory_exists(value.c_str())) return false;
        AcquireSRWLockExclusive(&g_lock);
        g_directory.swap(value); g_entries.clear();
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    } catch (...) { return false; }
}
bool initialize_storage() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_directory.empty()) { ReleaseSRWLockExclusive(&g_lock); return true; }
    bool result = false;
    try {
        std::array<wchar_t, 32768> module{};
        const auto size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        if (size != 0 && size < module.size()) {
            std::wstring path(module.data(), size);
            const auto split = path.find_last_of(L"\\/");
            if (split != std::wstring::npos) {
                path.resize(split);
                result = true;
                for (const auto component : {L"\\Restoration", L"\\cache", L"\\transport-certificates"}) {
                    path += component;
                    if (!CreateDirectoryW(path.c_str(), nullptr) && !directory_exists(path.c_str())) {
                        result = false; break;
                    }
                }
                if (result) g_directory = std::move(path);
            }
        }
    } catch (...) { result = false; }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}
bool publish(std::uint64_t platform, std::span<const std::byte> signedCertificate,
             std::uint64_t nowSeconds, bool* durable) noexcept {
    if (durable != nullptr) *durable = false;
    Entry incoming{};
    if (!wire::signed_metadata(signedCertificate, platform, nowSeconds, incoming.metadata)) return false;
    std::copy(signedCertificate.begin(), signedCertificate.end(), incoming.certificate.bytes.begin());
    incoming.certificate.size = signedCertificate.size();
    AcquireSRWLockExclusive(&g_lock);
    bool accepted = false;
    try {
        std::erase_if(g_entries, [nowSeconds](const Entry& entry) { return entry.metadata.expires <= nowSeconds; });
        Entry* slot = nullptr;
        for (auto& entry : g_entries) {
            if (entry.metadata.platform == platform && entry.metadata.key == incoming.metadata.key) { slot = &entry; break; }
        }
        if (slot != nullptr && slot->metadata.expires > incoming.metadata.expires) incoming = *slot;
        bool persisted = false;
        if (!g_directory.empty()) {
            const auto path = filename(platform, incoming.metadata.key);
            Entry previous{};
            if (load(path, platform, incoming.metadata.key, nowSeconds, previous)
                && previous.metadata.expires >= incoming.metadata.expires) {
                incoming = previous; persisted = store(path, incoming.certificate);
            } else {
                persisted = store(path, incoming.certificate);
            }
        }
        if (slot == nullptr) { g_entries.push_back(incoming); }
        else { *slot = incoming; }
        accepted = true;
        if (durable != nullptr) *durable = persisted;
    } catch (...) { accepted = false; }
    ReleaseSRWLockExclusive(&g_lock);
    return accepted;
}
bool find(std::uint64_t platform, std::span<const std::byte> svc304,
          std::uint64_t nowSeconds, Snapshot& output, const char** reason) noexcept {
    output = {};
    if (reason != nullptr) *reason = "invalid_request";
    std::array<std::byte, 32> key{};
    if (platform == 0 || !wire::request_key(svc304, key)) return false;
    bool found = false;
    const char* why = "no_certificate";
    AcquireSRWLockShared(&g_lock);
    for (const auto& entry : g_entries) {
        if (entry.certificate.size != 0 && entry.metadata.platform == platform) {
            if (entry.metadata.key == key) why = "expired";
            else if (why != std::string_view("expired")) why = "key_mismatch";
        }
        if (entry.certificate.size != 0 && entry.metadata.platform == platform
            && entry.metadata.key == key && entry.metadata.expires > nowSeconds) {
            output = entry.certificate; found = true; why = "matched"; break;
        }
    }
    if (!found && !g_directory.empty()) {
        try {
            Entry stored{};
            if (load(filename(platform, key), platform, key, nowSeconds, stored)) {
                output = stored.certificate; found = true; why = "matched_durable";
            }
        } catch (...) {   }
    }
    ReleaseSRWLockShared(&g_lock);
    if (reason != nullptr) *reason = why;
    return found;
}
void clear() noexcept {
    AcquireSRWLockExclusive(&g_lock); g_entries.clear(); ReleaseSRWLockExclusive(&g_lock);
}
}
