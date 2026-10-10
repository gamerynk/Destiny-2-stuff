#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace dawn::client::network {

// 256 KiB fixed storage; the Client's one-way BAP frames are large
inline constexpr std::size_t kBapFrameCapacity = 256 * 1024;

// one landed player can hold primary, private activity and several public activity links;
// the dedicated process serves many players, so it gets more slots than the 4-slot client
#if defined(RESTORATION_DEDICATED_SERVER)
inline constexpr std::size_t kBapConnectionCount = 32;
#else
inline constexpr std::size_t kBapConnectionCount = 4;
#endif

enum class HttpMethod : std::uint8_t {
    get,
    post,
};

struct HttpRequest {
    HttpMethod method{HttpMethod::post};
    std::string_view url;
    std::string_view contentType;
    // raw request header block, CRLF separated, no request line. Only the transport listener
    // fills it. A header is a caller's claim, never proof of identity
    std::string_view headers;
    std::span<const std::byte> body;
    std::span<std::byte> response;
};

struct HttpResponse {
    std::size_t size{};
    unsigned statusCode{};
    std::string_view contentType{"application/octet-stream"};
};

enum class BapEvent : std::uint8_t {
    open,
    frame,
    poll,
    close,
};

struct BapRequest {
    BapEvent event{};
    std::uint32_t connectionId{};
    std::span<const std::byte> frame{};
    std::span<std::byte> response{};
};

struct BapResponse {
    std::size_t size{};
};

} // namespace dawn::client::network
