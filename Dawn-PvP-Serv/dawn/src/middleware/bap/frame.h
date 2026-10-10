#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace dawn::middleware::bap {

enum class FrameType : std::uint8_t {
    plaintext0 = 0,
    encrypted = 1,
    plaintext2 = 2,
};

enum class RequestService : std::uint16_t {
    activityHostManager = 6,
    activityMessage = 8,
    webService = 10,
    webServiceServer = 110,
    subscribeFamily = 12,
    unsubscribeFamily = 14,
    activityHost = 16,
    clientConfig = 18,
    purchasedOffers = 21,
    accountTranslation = 23,
    serverHello = 25,
    notification29 = 29,
    start = 30,
    userMessage = 32,
    skill = 34,
    request36 = 36,
    request38 = 38,
    request40 = 40,
    matchmaking = 42,
    request48 = 48,
    clan = 44,
    registerSubscriber = 121,
    notification171 = 171,
    echo = 250,
    registerRelayClient = 302,
    signSteamCertificate = 304,
    accountFromMembership = 306,
};

enum class ResponseService : std::uint16_t {
    activityHostManager = 7,
    webService = 11,
    webServiceServer = 112,
    subscribeFamily = 13,
    unsubscribeFamily = 15,
    activityHost = 17,
    clientConfig = 19,
    purchasedOffers = 22,
    accountTranslation = 24,
    serverHello = 26,
    start = 31,
    userMessage = 33,
    skill = 35,
    response37 = 37,
    response39 = 39,
    response41 = 41,
    matchmaking = 43,
    response49 = 49,
    clan = 45,
    registerSubscriber = 122,
    echo = 251,
    registerRelayClient = 303,
    signSteamCertificate = 305,
    accountFromMembership = 307,
};

enum class NotificationService : std::uint16_t {
    activityMessage = 9,
    queuezUpdate = 123,
};

struct RequestFrame {
    FrameType frameType{};
    std::uint16_t messageId{};
    std::uint32_t taskId{};
    std::span<const std::byte> body{};
};

struct OuterFrame {
    FrameType frameType{};
    std::span<const std::byte> payload{};
};

[[nodiscard]] bool parse_frame(std::span<const std::byte> input, OuterFrame& frame) noexcept;

[[nodiscard]] bool parse_request_payload(std::span<const std::byte> input,
                                         FrameType frameType,
                                         RequestFrame& request) noexcept;

[[nodiscard]] bool parse_request(std::span<const std::byte> input, RequestFrame& request) noexcept;

[[nodiscard]] bool encode_response(ResponseService service,
                                   std::uint32_t taskId,
                                   FrameType frameType,
                                   std::span<const std::byte> body,
                                   std::span<std::byte> output,
                                   std::size_t& written) noexcept;

[[nodiscard]] bool encode_response_payload(ResponseService service,
                                           std::uint32_t taskId,
                                           std::span<const std::byte> body,
                                           std::span<std::byte> output,
                                           std::size_t& written,
                                           std::uint16_t status = 200) noexcept;

[[nodiscard]] bool encode_notification_payload(NotificationService service,
                                               std::uint32_t sequence,
                                               std::span<const std::byte> body,
                                               std::span<std::byte> output,
                                               std::size_t& written) noexcept;

[[nodiscard]] bool encode_frame(FrameType frameType,
                                std::span<const std::byte> payload,
                                std::span<std::byte> output,
                                std::size_t& written) noexcept;

} // namespace dawn::middleware::bap
