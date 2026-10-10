#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../crypto/hmac.h"
#include "association_keys.h"

namespace dawn::middleware::gameplay::dtls {

inline constexpr std::size_t kRecordPrefixSize = 18;
inline constexpr std::size_t kRecordTagSize = 8;
inline constexpr std::size_t kRecordPayloadCapacity = 1400;
inline constexpr std::size_t kRecordCapacity = kRecordPrefixSize + kRecordPayloadCapacity + 16;

struct RecordContext {
    Keys keys{};
    crypto::hmac::Algorithm authAlgorithm{crypto::hmac::Algorithm::sha256};
    std::uint16_t sendTag{};
};

[[nodiscard]] bool read_record_tag(std::span<const std::byte> datagram,
                                   std::uint16_t& tag) noexcept;

[[nodiscard]] bool open(const RecordContext& context,
                        std::span<const std::byte> datagram,
                        std::span<std::byte> plaintext,
                        std::size_t& size,
                        std::uint32_t& sequence) noexcept;

[[nodiscard]] bool seal(const RecordContext& context,
                        std::uint32_t sequence,
                        std::span<const std::byte> payload,
                        std::span<std::byte> datagram,
                        std::size_t& size) noexcept;

// the peer never names its digest on the wire; identify once, hold it per association
[[nodiscard]] bool identify_auth(const Keys& keys,
                                 std::span<const std::byte> datagram,
                                 crypto::hmac::Algorithm& output) noexcept;

} // namespace dawn::middleware::gameplay::dtls
