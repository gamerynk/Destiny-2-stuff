#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
namespace dawn::middleware::bap::native_certificate_publication {
// optional prefix after the proof; old requests and empty ACKs stay unchanged, so a new
// client must never infer persistence from an old ACK
inline constexpr std::array<std::byte, 4> kRequest{std::byte{'D'},std::byte{'N'},std::byte{'C'},std::byte{1}};
inline constexpr std::array<std::byte, 4> kDurableAck{std::byte{'D'},std::byte{'N'},std::byte{'C'},std::byte{1}};
inline constexpr std::array<std::byte, 4> kVolatileAck{std::byte{'D'},std::byte{'N'},std::byte{'C'},std::byte{0}};
inline bool requested(std::span<const std::byte> body) noexcept {
    return body.size() >= kRequest.size() && std::equal(kRequest.begin(), kRequest.end(), body.begin());
}
inline bool acknowledgement(std::span<const std::byte> body, bool& durable) noexcept {
    durable = false;
    if (body.size() != kDurableAck.size()) return false;
    if (std::equal(kDurableAck.begin(), kDurableAck.end(), body.begin())) { durable = true; return true; }
    return std::equal(kVolatileAck.begin(), kVolatileAck.end(), body.begin());
}
}
