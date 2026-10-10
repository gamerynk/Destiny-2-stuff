#include "account_translation_response.h"

#include <array>
#include "../../encoding/byte_order.h"

namespace dawn::middleware::bap::account_translation {
namespace {
constexpr std::size_t kHeaderSize = 4;
constexpr std::size_t kIdentitySize = 8;
constexpr std::size_t kPairSize = 16;
constexpr std::byte kPlatformType{12};
constexpr std::byte kAccountType{255};

bool encode_empty(std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    if (output.size() < kHeaderSize) return false;
    encoding::write_u16_be(output.first<2>(), 0);
    output[2] = kAccountType;
    output[3] = kAccountType;
    written = kHeaderSize;
    return true;
}

std::size_t request_count(std::span<const std::byte> body) noexcept {
    if (body.size() < kHeaderSize || body[2] != kPlatformType || body[3] != kAccountType)
        return 0;
    const auto count = encoding::read_u16_be(body.first<2>());
    if (count > kMaximumEntries || body.size() != kHeaderSize + count * kIdentitySize)
        return 0;
    return count;
}
} // namespace

bool request_platform_id(std::span<const std::byte> body, std::uint64_t& platformId) noexcept {
    platformId = 0;
    // a scalar caller must never attach its account to the first member of a batch
    if (request_count(body) != 1) return false;
    platformId = encoding::read_u64_be(body.subspan<4, 8>());
    return platformId != 0;
}

bool request_matches_platform(std::span<const std::byte> body, std::uint64_t platformId) noexcept {
    std::uint64_t requested = 0;
    return platformId != 0 && request_platform_id(body, requested) && requested == platformId;
}

bool encode_response(std::span<const std::byte> body, std::uint64_t accountSoid,
                     std::span<std::byte> output, std::size_t& written) noexcept {
    std::uint64_t platformId = 0;
    if (accountSoid == 0 || !request_platform_id(body, platformId))
        return encode_empty(output, written);
    const auto resolve = +[](void* context, std::uint64_t) noexcept {
        return *static_cast<const std::uint64_t*>(context);
    };
    return encode_response(body, &accountSoid, resolve, output, written);
}

bool encode_response(std::span<const std::byte> body, void* context, AccountResolver resolve,
                     std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    const auto count = request_count(body);
    if (count == 0 || resolve == nullptr) return encode_empty(output, written);
    struct Pair { std::uint64_t platform; std::uint64_t account; };
    std::array<Pair, kMaximumEntries> pairs{};
    std::size_t answered = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto platform = encoding::read_u64_be(body.subspan(kHeaderSize + index * 8).first<8>());
        if (platform == 0) continue;
        const auto account = resolve(context, platform);
        // a missing row must not stop later identities (including the local one) resolving
        if (account != 0) pairs[answered++] = {platform, account};
    }
    if (answered == 0) return encode_empty(output, written);
    const auto required = kHeaderSize + answered * kPairSize;
    // resolve first so a short destination never publishes a partial roster
    if (output.size() < required) return false;
    encoding::write_u16_be(output.first<2>(), static_cast<std::uint16_t>(answered));
    output[2] = kPlatformType;
    output[3] = kAccountType;
    // native svc-24 has two parallel arrays, not interleaved pairs; compact both to the
    // resolved count when a row is omitted
    const auto accountsOffset = kHeaderSize + answered * kIdentitySize;
    for (std::size_t index = 0; index < answered; ++index) {
        const auto offset = index * kIdentitySize;
        encoding::write_u64_be(output.subspan(kHeaderSize + offset).first<8>(), pairs[index].platform);
        encoding::write_u64_be(output.subspan(accountsOffset + offset).first<8>(), pairs[index].account);
    }
    written = required;
    return true;
}
} // namespace dawn::middleware::bap::account_translation
