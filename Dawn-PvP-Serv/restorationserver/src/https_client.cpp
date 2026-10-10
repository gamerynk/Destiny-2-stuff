#include "https_client.h"

#include <Windows.h>
#include <winhttp.h>

#include <cstddef>

namespace restoration::https_client {
namespace {

constexpr std::size_t kMaximumBodySize = 8 * 1024 * 1024;
constexpr int kResolveTimeoutMilliseconds = 5000;
constexpr int kConnectTimeoutMilliseconds = 5000;
constexpr int kSendTimeoutMilliseconds = 8000;
constexpr int kReceiveTimeoutMilliseconds = 12000;

SRWLOCK g_lock{SRWLOCK_INIT};
HINTERNET g_session{};

class Handle final {
public:
    explicit Handle(HINTERNET value) noexcept : value_(value) {}
    ~Handle() {
        if (value_ != nullptr) {
            (void)WinHttpCloseHandle(value_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] HINTERNET get() const noexcept {
        return value_;
    }

private:
    HINTERNET value_{};
};

} // namespace

bool initialize() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_session == nullptr) {
        g_session = WinHttpOpen(L"DawnServer/1.0",
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME,
                                WINHTTP_NO_PROXY_BYPASS,
                                0);
        if (g_session != nullptr) {
            (void)WinHttpSetTimeouts(g_session,
                                     kResolveTimeoutMilliseconds,
                                     kConnectTimeoutMilliseconds,
                                     kSendTimeoutMilliseconds,
                                     kReceiveTimeoutMilliseconds);
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            (void)WinHttpSetOption(
                g_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
    }
    const bool ready = g_session != nullptr;
    ReleaseSRWLockExclusive(&g_lock);
    return ready;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_session != nullptr) {
        (void)WinHttpCloseHandle(g_session);
        g_session = nullptr;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool widen_ascii(std::string_view input, std::wstring& output) noexcept {
    output.clear();
    output.reserve(input.size());
    for (const char character : input) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte > 0x7E) {
            output.clear();
            return false;
        }
        output.push_back(static_cast<wchar_t>(byte));
    }
    return true;
}

bool get(std::wstring_view host,
         std::wstring_view path,
         std::wstring_view headers,
         Response& output) noexcept {
    output = {};
    const std::wstring hostText(host);
    const std::wstring pathText(path);
    const std::wstring headerText(headers);

    AcquireSRWLockShared(&g_lock);
    bool success = false;
    DWORD status = 0;
    if (g_session != nullptr) {
        const Handle connection(
            WinHttpConnect(g_session, hostText.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        const Handle request(connection.get() == nullptr
                                 ? nullptr
                                 : WinHttpOpenRequest(connection.get(),
                                                      L"GET",
                                                      pathText.c_str(),
                                                      nullptr,
                                                      WINHTTP_NO_REFERER,
                                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                      WINHTTP_FLAG_SECURE));
        DWORD statusSize = sizeof status;
        success = request.get() != nullptr
                  && WinHttpSendRequest(request.get(),
                                        headerText.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                           : headerText.c_str(),
                                        headerText.empty() ? 0 : static_cast<DWORD>(-1L),
                                        WINHTTP_NO_REQUEST_DATA,
                                        0,
                                        0,
                                        0)
                         != FALSE
                  && WinHttpReceiveResponse(request.get(), nullptr) != FALSE
                  && WinHttpQueryHeaders(request.get(),
                                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                         WINHTTP_HEADER_NAME_BY_INDEX,
                                         &status,
                                         &statusSize,
                                         WINHTTP_NO_HEADER_INDEX)
                         != FALSE;
        while (success) {
            DWORD available = 0;
            if (WinHttpQueryDataAvailable(request.get(), &available) == FALSE) {
                success = false;
                break;
            }
            if (available == 0) {
                break;
            }
            const std::size_t offset = output.body.size();
            if (available > kMaximumBodySize - offset) {
                success = false;
                break;
            }
            output.body.resize(offset + available);
            DWORD read = 0;
            if (WinHttpReadData(request.get(), output.body.data() + offset, available, &read)
                == FALSE) {
                success = false;
                break;
            }
            output.body.resize(offset + read);
        }
    }
    ReleaseSRWLockShared(&g_lock);

    if (!success) {
        output = {};
        return false;
    }
    output.status = status;
    return true;
}

} // namespace restoration::https_client
