#include "transport.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/settings/settings.h"

namespace dawn::client::hooks::external_server {
namespace {

constexpr DWORD kSocketTimeoutMilliseconds = 3000;
// response headers are tiny; the cap rejects an unbounded or malformed peer
constexpr std::size_t kHeaderCapacity = 16 * 1024;
constexpr std::size_t kRequestHeaderCapacity = 2048;
constexpr std::string_view kHeaderTerminator = "\r\n\r\n";

class WinsockLifetime final {
public:
    WinsockLifetime() noexcept {
        WSADATA data{};
        active_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockLifetime() {
        if (active_) {
            WSACleanup();
        }
    }

    WinsockLifetime(const WinsockLifetime&) = delete;
    WinsockLifetime& operator=(const WinsockLifetime&) = delete;

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

private:
    bool active_{};
};

class SocketLifetime final {
public:
    explicit SocketLifetime(SOCKET value) noexcept : value_(value) {}

    ~SocketLifetime() {
        if (value_ != INVALID_SOCKET) {
            closesocket(value_);
        }
    }

    SocketLifetime(const SocketLifetime&) = delete;
    SocketLifetime& operator=(const SocketLifetime&) = delete;

    [[nodiscard]] SOCKET get() const noexcept {
        return value_;
    }

private:
    SOCKET value_{INVALID_SOCKET};
};

// one monotonic deadline covers connect, both sends and the whole response
class ExchangeDeadline final {
public:
    ExchangeDeadline() noexcept : expires_(GetTickCount64() + kSocketTimeoutMilliseconds) {}
    [[nodiscard]] bool wait(SOCKET value, bool writing) const noexcept {
        for (;;) {
            const auto now = GetTickCount64();
            if (now >= expires_) return false;
            const auto remaining = expires_ - now;
            timeval timeout{static_cast<long>(remaining / 1000),
                            static_cast<long>((remaining % 1000) * 1000)};
            fd_set ready{}, errors{};
            FD_ZERO(&ready); FD_SET(value, &ready);
            FD_ZERO(&errors); FD_SET(value, &errors);
            const int result = select(0, writing ? nullptr : &ready,
                                      writing ? &ready : nullptr, &errors, &timeout);
            if (result == SOCKET_ERROR && WSAGetLastError() == WSAEINTR) continue;
            return result > 0 && !FD_ISSET(value, &errors) && FD_ISSET(value, &ready)
                   && GetTickCount64() < expires_;
        }
    }
private:
    ULONGLONG expires_{};
};

[[nodiscard]] bool connect_until(SOCKET value, const sockaddr_in& address,
                                 const ExchangeDeadline& deadline) noexcept {
    if (connect(value, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0)
        return true;
    const int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS && error != WSAEALREADY)
        return false;
    if (!deadline.wait(value, true)) return false;
    int completion{}, length = sizeof completion;
    return getsockopt(value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&completion),
                      &length) == 0 && completion == 0;
}

[[nodiscard]] int receive_until(SOCKET value, char* output, int count,
                                const ExchangeDeadline& deadline) noexcept {
    for (;;) {
        if (!deadline.wait(value, false)) return SOCKET_ERROR;
        const int result = recv(value, output, count, 0);
        if (result != SOCKET_ERROR) return result;
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINTR) return SOCKET_ERROR;
    }
}

// partial progress must not reset the shared deadline
[[nodiscard]] bool send_all(SOCKET socketValue, std::span<const std::byte> bytes,
                            const ExchangeDeadline& deadline) noexcept {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        if (!deadline.wait(socketValue, true)) return false;
        const int requestSize = static_cast<int>((std::min)(bytes.size() - offset,
                                                            static_cast<std::size_t>(INT_MAX)));
        const int sent = send(socketValue, reinterpret_cast<const char*>(bytes.data() + offset),
                              requestSize, 0);
        if (sent == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAEINTR) continue;
            return false;
        }
        if (sent == 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

[[nodiscard]] bool ascii_equal(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool parse_size(std::string_view text,
                              std::size_t capacity,
                              std::size_t& output) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (digit > capacity || value > (capacity - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    output = value;
    return true;
}

[[nodiscard]] bool request_path(std::string_view url, std::string_view& output) noexcept {
    if (url.empty() || url.find_first_of("\r\n") != std::string_view::npos) {
        return false;
    }
    if (url.front() == '/') {
        output = url;
        return true;
    }
    const std::size_t scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return false;
    }
    const std::size_t authority = scheme + 3;
    const std::size_t path = url.find('/', authority);
    output = path == std::string_view::npos ? std::string_view{"/"} : url.substr(path);
    return !output.empty();
}

[[nodiscard]] bool parse_response_header(std::string_view header,
                                         std::size_t capacity,
                                         unsigned& status,
                                         std::size_t& contentLength) noexcept {
    const std::size_t firstLineEnd = header.find("\r\n");
    const std::string_view firstLine = header.substr(0, firstLineEnd);
    const std::size_t firstSpace = firstLine.find(' ');
    if (!firstLine.starts_with("HTTP/1.") || firstSpace == std::string_view::npos
        || firstSpace + 4 > firstLine.size()) {
        return false;
    }
    const std::string_view statusText = firstLine.substr(firstSpace + 1, 3);
    if (statusText[0] < '0' || statusText[0] > '9' || statusText[1] < '0'
        || statusText[1] > '9' || statusText[2] < '0' || statusText[2] > '9') {
        return false;
    }
    status = static_cast<unsigned>(statusText[0] - '0') * 100U
             + static_cast<unsigned>(statusText[1] - '0') * 10U
             + static_cast<unsigned>(statusText[2] - '0');

    bool foundLength = false;
    std::size_t lineStart = firstLineEnd == std::string_view::npos
                                ? header.size()
                                : firstLineEnd + 2;
    while (lineStart < header.size()) {
        const std::size_t lineEnd = header.find("\r\n", lineStart);
        const std::size_t end = lineEnd == std::string_view::npos ? header.size() : lineEnd;
        const std::string_view line = header.substr(lineStart, end - lineStart);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            return false;
        }
        const std::string_view name = trim(line.substr(0, colon));
        const std::string_view value = trim(line.substr(colon + 1));
        if (ascii_equal(name, "Content-Length")) {
            if (foundLength || !parse_size(value, capacity, contentLength)) {
                return false;
            }
            foundLength = true;
        } else if (ascii_equal(name, "Transfer-Encoding")) {
            return false;
        }
        lineStart = end + 2;
    }
    return foundLength;
}

[[nodiscard]] bool receive_response(SOCKET socketValue,
                                    std::span<std::byte> output,
                                    network::HttpResponse& response,
                                    const ExchangeDeadline& deadline) noexcept {
    std::array<char, kHeaderCapacity> prefix{};
    std::size_t prefixSize = 0;
    std::size_t marker = std::string_view::npos;
    while (marker == std::string_view::npos) {
        if (prefixSize == prefix.size()) {
            return false;
        }
        const int received = receive_until(socketValue,
                                  prefix.data() + prefixSize,
                                  static_cast<int>(prefix.size() - prefixSize),
                                  deadline);
        if (received <= 0) {
            return false;
        }
        prefixSize += static_cast<std::size_t>(received);
        marker = std::string_view(prefix.data(), prefixSize).find(kHeaderTerminator);
    }

    const std::size_t bodyStart = marker + kHeaderTerminator.size();
    unsigned status{};
    std::size_t contentLength{};
    if (!parse_response_header({prefix.data(), marker}, output.size(), status, contentLength)) {
        return false;
    }
    const std::size_t buffered = prefixSize - bodyStart;
    if (buffered > contentLength) {
        return false;
    }
    if (buffered != 0) {
        std::copy_n(reinterpret_cast<const std::byte*>(prefix.data() + bodyStart),
                    buffered,
                    output.begin());
    }
    std::size_t bodySize = buffered;
    while (bodySize < contentLength) {
        const std::size_t remaining = contentLength - bodySize;
        const int requestSize = static_cast<int>((std::min)(
            remaining, static_cast<std::size_t>(INT_MAX)));
        const int received = receive_until(socketValue,
                                  reinterpret_cast<char*>(output.data() + bodySize),
                                  requestSize,
                                  deadline);
        if (received <= 0) {
            return false;
        }
        bodySize += static_cast<std::size_t>(received);
    }
    response.size = bodySize;
    response.statusCode = status;
    response.contentType = "application/octet-stream";
    return true;
}

} // namespace

bool consume(const network::HttpRequest& request, network::HttpResponse& response) noexcept {
    response = {};
    std::string_view path;
    if (!request_path(request.url, path)
        || request.contentType.find_first_of("\r\n") != std::string_view::npos) {
        return false;
    }
    const auto& configured = core::settings::get().client.externalServer;
    WinsockLifetime winsock;
    if (!winsock.active()) {
        return false;
    }
    SocketLifetime connection(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (connection.get() == INVALID_SOCKET) {
        return false;
    }
    const ExchangeDeadline deadline;
    u_long nonblocking = 1;
    const int noDelay = 1;
    if (ioctlsocket(connection.get(), FIONBIO, &nonblocking) != 0
        || setsockopt(connection.get(), IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&noDelay), sizeof noDelay) != 0) return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(configured.httpPort);
    if (inet_pton(AF_INET, configured.host.data(), &address.sin_addr) != 1
        || !connect_until(connection.get(), address, deadline)) {
        return false;
    }

    const std::string_view method = request.method == network::HttpMethod::get ? "GET" : "POST";
    const std::string_view contentType =
        request.contentType.empty() ? std::string_view{"application/octet-stream"}
                                    : request.contentType;
    std::array<char, kRequestHeaderCapacity> header{};
    const int headerSize = std::snprintf(header.data(),
                                         header.size(),
                                         "%.*s %.*s HTTP/1.1\r\n"
                                         "Host: %s:%u\r\n"
                                         "Content-Type: %.*s\r\n"
                                         "Content-Length: %zu\r\n"
                                         "Connection: close\r\n\r\n",
                                         static_cast<int>(method.size()),
                                         method.data(),
                                         static_cast<int>(path.size()),
                                         path.data(),
                                         configured.host.data(),
                                         static_cast<unsigned>(configured.httpPort),
                                         static_cast<int>(contentType.size()),
                                         contentType.data(),
                                         request.body.size());
    if (headerSize <= 0 || static_cast<std::size_t>(headerSize) >= header.size()
        || !send_all(connection.get(),
                     std::as_bytes(std::span(header.data(), static_cast<std::size_t>(headerSize))), deadline)
        || !send_all(connection.get(), request.body, deadline)) {
        return false;
    }
    return receive_response(connection.get(), request.response, response, deadline);
}

} // namespace dawn::client::hooks::external_server
