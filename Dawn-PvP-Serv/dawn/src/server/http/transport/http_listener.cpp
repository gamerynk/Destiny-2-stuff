#include "../../public_bind.h"
#include "http_listener.h"

#include <WS2tcpip.h>
#include <WinSock2.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

#include "../../../client/network/consumer.h"
#include "../../../core/logging/log.h"
#include "../../../core/settings/settings.h"
#include "../server_http.h"

namespace dawn::server::http::transport {
namespace {

constexpr std::size_t kRequestCapacity = 64 * 1024;
constexpr std::size_t kResponseCapacity = 2 * 1024 * 1024;
constexpr std::size_t kHeaderCapacity = 512;
constexpr std::size_t kPeerCount = 8;
constexpr std::string_view kHeaderTerminator = "\r\n\r\n";

enum class ParseResult : std::uint8_t {
    incomplete,
    ready,
    invalid,
};

struct ParsedRequest final {
    client::network::HttpMethod method{client::network::HttpMethod::get};
    std::string_view path;
    std::string_view contentType;
    std::string_view headers;
    std::span<const std::byte> body;
};

struct Peer final {
    SOCKET socket{INVALID_SOCKET};
    std::array<std::byte, kRequestCapacity> request{};
    std::size_t requestSize{};
    std::array<char, kHeaderCapacity> header{};
    std::size_t headerOffset{};
    std::size_t headerSize{};
    std::array<std::byte, kResponseCapacity> response{};
    std::size_t responseOffset{};
    std::size_t responseSize{};
    bool routed{};
    bool publicRoutes{};
};

struct Listener final {
    SOCKET acceptor{INVALID_SOCKET};
    std::array<Peer, kPeerCount> peers{};
    std::uint16_t port{};
    bool winsockOwned{};
    bool active{};
    bool publicRoutes{};
};

Listener g_privateListener;
Listener g_publicListener;
SRWLOCK g_lock{SRWLOCK_INIT};

[[nodiscard]] bool make_nonblocking(SOCKET socketValue) noexcept {
    u_long enabled = 1;
    return ioctlsocket(socketValue, FIONBIO, &enabled) != SOCKET_ERROR;
}

void close_peer(Peer& peer) noexcept {
    if (peer.socket != INVALID_SOCKET) {
        closesocket(peer.socket);
    }
    peer.socket = INVALID_SOCKET;
    peer.requestSize = 0;
    peer.headerOffset = 0;
    peer.headerSize = 0;
    peer.responseOffset = 0;
    peer.responseSize = 0;
    peer.routed = false;
    peer.publicRoutes = false;
}

[[nodiscard]] Peer* free_peer(Listener& listener) noexcept {
    for (Peer& peer : listener.peers) {
        if (peer.socket == INVALID_SOCKET) {
            return &peer;
        }
    }
    return nullptr;
}

void accept_one(Listener& listener) noexcept {
    Peer* const peer = free_peer(listener);
    if (peer == nullptr) {
        return;
    }
    const SOCKET accepted = accept(listener.acceptor, nullptr, nullptr);
    if (accepted == INVALID_SOCKET) {
        return;
    }
    if (!make_nonblocking(accepted)) {
        closesocket(accepted);
        return;
    }
    close_peer(*peer);
    peer->socket = accepted;
    peer->publicRoutes = listener.publicRoutes;
}

[[nodiscard]] std::size_t find_bytes(std::span<const std::byte> bytes,
                                     std::string_view needle) noexcept {
    if (needle.empty() || bytes.size() < needle.size()) {
        return std::string_view::npos;
    }
    const auto* text = reinterpret_cast<const char*>(bytes.data());
    return std::string_view(text, bytes.size()).find(needle);
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

[[nodiscard]] bool parse_size(std::string_view text, std::size_t& output) noexcept {
    if (text.empty()) {
        return false;
    }
    std::size_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (value > (kRequestCapacity - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    output = value;
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

[[nodiscard]] ParseResult parse_request(const Peer& peer, ParsedRequest& output) noexcept {
    const std::span<const std::byte> request(peer.request.data(), peer.requestSize);
    const std::size_t headerMarker = find_bytes(request, kHeaderTerminator);
    if (headerMarker == std::string_view::npos) {
        return peer.requestSize == peer.request.size() ? ParseResult::invalid
                                                      : ParseResult::incomplete;
    }
    const std::size_t headerSize = headerMarker + kHeaderTerminator.size();
    const auto* chars = reinterpret_cast<const char*>(request.data());
    const std::string_view headers(chars, headerMarker);
    const std::size_t firstLineEnd = headers.find("\r\n");
    const std::string_view firstLine =
        firstLineEnd == std::string_view::npos ? headers : headers.substr(0, firstLineEnd);
    const std::size_t firstSpace = firstLine.find(' ');
    const std::size_t secondSpace = firstLine.find(' ', firstSpace + 1);
    if (firstSpace == std::string_view::npos || secondSpace == std::string_view::npos
        || secondSpace <= firstSpace + 1) {
        return ParseResult::invalid;
    }
    const std::string_view method = firstLine.substr(0, firstSpace);
    if (method == "GET") {
        output.method = client::network::HttpMethod::get;
    } else if (method == "POST") {
        output.method = client::network::HttpMethod::post;
    } else {
        return ParseResult::invalid;
    }
    output.path = firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);

    std::size_t contentLength = 0;
    std::size_t lineStart = firstLineEnd == std::string_view::npos
                                ? headers.size()
                                : firstLineEnd + 2;
    // published to routes that authenticate from a header, such as the Platform API key
    output.headers = headers.substr(lineStart);
    while (lineStart < headers.size()) {
        const std::size_t lineEnd = headers.find("\r\n", lineStart);
        const std::size_t end =
            lineEnd == std::string_view::npos ? headers.size() : lineEnd;
        const std::string_view line = headers.substr(lineStart, end - lineStart);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) {
            return ParseResult::invalid;
        }
        const std::string_view name = trim(line.substr(0, colon));
        const std::string_view value = trim(line.substr(colon + 1));
        if (ascii_equal(name, "Content-Length") && !parse_size(value, contentLength)) {
            return ParseResult::invalid;
        }
        if (ascii_equal(name, "Content-Type")) {
            output.contentType = value;
        }
        lineStart = end + 2;
    }
    if (contentLength > peer.request.size() - headerSize) {
        return ParseResult::invalid;
    }
    if (peer.requestSize < headerSize + contentLength) {
        return ParseResult::incomplete;
    }
    output.body = request.subspan(headerSize, contentLength);
    return ParseResult::ready;
}

[[nodiscard]] const char* reason_phrase(unsigned status) noexcept {
    switch (status) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 503:
        return "Service Unavailable";
    default:
        return "Internal Server Error";
    }
}

void prepare_response(Peer& peer,
                      unsigned status,
                      std::string_view contentType,
                      std::size_t bodySize) noexcept {
    if (bodySize > peer.response.size()) {
        status = 500;
        bodySize = 0;
        contentType = "application/octet-stream";
    }
    const int written = std::snprintf(peer.header.data(),
                                      peer.header.size(),
                                      "HTTP/1.1 %u %s\r\n"
                                      "Content-Length: %zu\r\n"
                                      "Content-Type: %.*s\r\n"
                                      "Connection: close\r\n\r\n",
                                      status,
                                      reason_phrase(status),
                                      bodySize,
                                      static_cast<int>(contentType.size()),
                                      contentType.data());
    if (written <= 0 || static_cast<std::size_t>(written) >= peer.header.size()) {
        close_peer(peer);
        return;
    }
    peer.headerOffset = 0;
    peer.headerSize = static_cast<std::size_t>(written);
    peer.responseOffset = 0;
    peer.responseSize = bodySize;
    peer.routed = true;
}

void prepare_static(Peer& peer, unsigned status, std::string_view body) noexcept {
    const std::size_t size = (std::min)(body.size(), peer.response.size());
    std::copy(body.begin(), body.begin() + static_cast<std::ptrdiff_t>(size),
              reinterpret_cast<char*>(peer.response.data()));
    prepare_response(peer, status, "application/json", size);
}

void route_peer(Peer& peer) noexcept {
    ParsedRequest parsed;
    const ParseResult result = parse_request(peer, parsed);
    if (result == ParseResult::incomplete) {
        return;
    }
    if (result == ParseResult::invalid) {
        prepare_static(peer, 400, "{\"error\":\"bad_request\"}\n");
        return;
    }
    const client::network::HttpRequest request{
        parsed.method,
        parsed.path,
        parsed.contentType,
        parsed.headers,
        parsed.body,
        peer.response,
    };
    client::network::HttpResponse response;
    const bool consumed = peer.publicRoutes ? http::consume_public(request, response)
                                            : http::consume(request, response);
    if (!consumed) {
        prepare_static(peer, 404, "{\"error\":\"not_found\"}\n");
        return;
    }
    prepare_response(peer, response.statusCode, response.contentType, response.size);
}

void receive_peer(Peer& peer) noexcept {
    if (peer.routed || peer.requestSize == peer.request.size()) {
        return;
    }
    const int received = recv(peer.socket,
                              reinterpret_cast<char*>(peer.request.data() + peer.requestSize),
                              static_cast<int>(peer.request.size() - peer.requestSize),
                              0);
    if (received > 0) {
        peer.requestSize += static_cast<std::size_t>(received);
        route_peer(peer);
        return;
    }
    if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
        close_peer(peer);
    }
}

[[nodiscard]] bool send_span(SOCKET socketValue,
                             const void* data,
                             std::size_t size,
                             std::size_t& offset) noexcept {
    if (offset >= size) {
        return true;
    }
    const auto* bytes = static_cast<const char*>(data);
    const int sent = send(socketValue, bytes + offset, static_cast<int>(size - offset), 0);
    if (sent > 0) {
        offset += static_cast<std::size_t>(sent);
        return true;
    }
    return sent == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK;
}

void send_peer(Peer& peer) noexcept {
    if (!peer.routed) {
        return;
    }
    if (!send_span(peer.socket, peer.header.data(), peer.headerSize, peer.headerOffset)) {
        close_peer(peer);
        return;
    }
    if (peer.headerOffset != peer.headerSize) {
        return;
    }
    if (!send_span(peer.socket,
                   peer.response.data(),
                   peer.responseSize,
                   peer.responseOffset)) {
        close_peer(peer);
        return;
    }
    if (peer.responseOffset == peer.responseSize) {
        close_peer(peer);
    }
}

} // namespace

[[nodiscard]] bool initialize_listener(Listener& listener,
                                       std::uint16_t port,
                                       bool publicRoutes) noexcept {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        return false;
    }
    listener.winsockOwned = true;
    listener.acceptor = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(publicRoutes ? server::public_bind_host_order() : INADDR_LOOPBACK);
    const bool opened = listener.acceptor != INVALID_SOCKET
                        && make_nonblocking(listener.acceptor)
                        && bind(listener.acceptor,
                                reinterpret_cast<const sockaddr*>(&address),
                                sizeof address)
                               != SOCKET_ERROR
                        && listen(listener.acceptor, static_cast<int>(kPeerCount)) != SOCKET_ERROR;
    if (!opened) {
        if (listener.acceptor != INVALID_SOCKET) {
            closesocket(listener.acceptor);
            listener.acceptor = INVALID_SOCKET;
        }
        WSACleanup();
        listener.winsockOwned = false;
        return false;
    }
    listener.port = port;
    listener.publicRoutes = publicRoutes;
    listener.active = true;
    std::array<char, 128> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=http stage=listen result=ok port=%u audience=%s",
                                      static_cast<unsigned>(port),
                                      publicRoutes ? "public" : "private");
    if (written > 0) {
        core::log::write(core::log::Channel::server,
                         core::log::Level::info,
                         {line.data(), static_cast<std::size_t>(written)});
    }
    return true;
}

void shutdown_listener(Listener& listener) noexcept {
    listener.active = false;
    if (listener.acceptor != INVALID_SOCKET) {
        closesocket(listener.acceptor);
        listener.acceptor = INVALID_SOCKET;
    }
    for (Peer& peer : listener.peers) {
        close_peer(peer);
    }
    if (listener.winsockOwned) {
        WSACleanup();
        listener.winsockOwned = false;
    }
    listener.port = 0;
    listener.publicRoutes = false;
}

bool initialize() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_privateListener.active) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    const auto& settings = core::settings::get().server;
    bool opened = initialize_listener(g_privateListener, settings.httpPort, false);
    if (opened && settings.publicHttpPort != 0) {
        opened = initialize_listener(g_publicListener, settings.publicHttpPort, true);
        if (!opened) {
            shutdown_listener(g_privateListener);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return opened;
}

void service_listener(Listener& listener) noexcept {
    if (!listener.active) {
        return;
    }
    fd_set readable;
    fd_set writable;
    FD_ZERO(&readable);
    FD_ZERO(&writable);
    if (free_peer(listener) != nullptr) {
        FD_SET(listener.acceptor, &readable);
    }
    for (const Peer& peer : listener.peers) {
        if (peer.socket == INVALID_SOCKET) {
            continue;
        }
        if (!peer.routed) {
            FD_SET(peer.socket, &readable);
        } else {
            FD_SET(peer.socket, &writable);
        }
    }
    timeval timeout{};
    if (select(0, &readable, &writable, nullptr, &timeout) == SOCKET_ERROR) {
        return;
    }
    if (FD_ISSET(listener.acceptor, &readable)) {
        accept_one(listener);
    }
    for (Peer& peer : listener.peers) {
        if (peer.socket == INVALID_SOCKET) {
            continue;
        }
        if (FD_ISSET(peer.socket, &readable)) {
            receive_peer(peer);
        }
        if (peer.socket != INVALID_SOCKET && FD_ISSET(peer.socket, &writable)) {
            send_peer(peer);
        }
    }
}

void service() noexcept {
    if (!TryAcquireSRWLockExclusive(&g_lock)) {
        return;
    }
    service_listener(g_privateListener);
    service_listener(g_publicListener);
    ReleaseSRWLockExclusive(&g_lock);
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    shutdown_listener(g_publicListener);
    shutdown_listener(g_privateListener);
    ReleaseSRWLockExclusive(&g_lock);
}

bool ready() noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool publicRequired = core::settings::get().server.publicHttpPort != 0;
    const bool active = g_privateListener.active
                        && (!publicRequired || g_publicListener.active);
    ReleaseSRWLockShared(&g_lock);
    return active;
}

} // namespace dawn::server::http::transport
