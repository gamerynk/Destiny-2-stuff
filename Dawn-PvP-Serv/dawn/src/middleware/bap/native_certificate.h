#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include "../protobuf/codec.h"

namespace dawn::middleware::bap::native_certificate {
inline constexpr std::size_t kCapacity = 768;
inline constexpr std::uint32_t kAppId = 1085660;
struct Metadata final {
    std::array<std::byte, 32> key{};
    std::uint64_t platform{};
    std::uint32_t created{}, expires{};
};

inline bool certificate_metadata(std::span<const std::byte> input, std::uint64_t platform,
                                 std::uint64_t now, Metadata& output) noexcept {
    output = {};
    protobuf::Reader reader(input);
    protobuf::Field field{};
    unsigned seen = 0;
    bool app = false, identity = false;
    while (reader.next(field)) {
        unsigned bit = 0;
        switch (field.fieldNumber) {
        case 1:
            bit = 1;
            if (field.wireType != protobuf::WireType::varint || field.value != 1) return false;
            break;
        case 2:
            bit = 2;
            if (field.wireType != protobuf::WireType::lengthDelimited || field.bytes.size() != 32) return false;
            std::copy(field.bytes.begin(), field.bytes.end(), output.key.begin());
            break;
        case 4:
            bit = 4;
            if (field.wireType != protobuf::WireType::fixed64 || field.value != platform) return false;
            identity = true;
            break;
        case 8:
            bit = 8;
            if (field.wireType != protobuf::WireType::fixed32 || field.value > now + 300) return false;
            output.created = static_cast<std::uint32_t>(field.value);
            break;
        case 9:
            bit = 16;
            if (field.wireType != protobuf::WireType::fixed32 || field.value <= now) return false;
            output.expires = static_cast<std::uint32_t>(field.value);
            break;
        case 10:
            if (field.wireType != protobuf::WireType::varint) return false;
            app |= field.value == kAppId;
            break;
        case 12: {
            bit = 32;
            if (field.wireType != protobuf::WireType::lengthDelimited) return false;
            const std::string_view text(reinterpret_cast<const char*>(field.bytes.data()), field.bytes.size());
            constexpr std::string_view prefix = "steamid:";
            if (!text.starts_with(prefix)) return false;
            std::uint64_t id{};
            const auto parsed = std::from_chars(text.data() + prefix.size(), text.data() + text.size(), id);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || id != platform) return false;
            identity = true;
            break;
        }
        default: break;
        }
        if (bit != 0 && (seen & bit) != 0) return false;
        seen |= bit;
    }
    output.platform = platform;
    return reader.remaining() == 0 && platform != 0 && identity && app && (seen & 27) == 27
        && output.created < output.expires;
}

// signed protobuf holds only public cert(4), CA id(5), signature(6); private field 1 refused
inline bool signed_metadata(std::span<const std::byte> input, std::uint64_t platform,
                            std::uint64_t now, Metadata& output) noexcept {
    if (input.empty() || input.size() > kCapacity) return false;
    protobuf::Reader reader(input);
    protobuf::Field field{};
    unsigned seen = 0;
    while (reader.next(field)) {
        unsigned bit{};
        if (field.fieldNumber == 4) {
            bit = 1;
            if (field.wireType != protobuf::WireType::lengthDelimited
                || !certificate_metadata(field.bytes, platform, now, output)) return false;
        } else if (field.fieldNumber == 5) {
            bit = 2;
            if (field.wireType != protobuf::WireType::fixed64 || field.value == 0) return false;
        } else if (field.fieldNumber == 6) {
            bit = 4;
            if (field.wireType != protobuf::WireType::lengthDelimited || field.bytes.size() != 64) return false;
        } else return false;
        if ((seen & bit) != 0) return false;
        seen |= bit;
    }
    return seen == 7 && reader.remaining() == 0;
}

// svc304 field3 -> CertificateRequest field1 -> cert field2
inline bool request_key(std::span<const std::byte> request, std::array<std::byte, 32>& key) noexcept {
    auto nested = [](std::span<const std::byte> input, std::uint32_t wanted,
                     std::span<const std::byte>& bytes) noexcept {
        protobuf::Reader reader(input); protobuf::Field field{}; bool found = false;
        while (reader.next(field)) {
            if (field.fieldNumber != wanted) continue;
            if (found || field.wireType != protobuf::WireType::lengthDelimited) return false;
            found = true; bytes = field.bytes;
        }
        return found && reader.remaining() == 0;
    };
    std::span<const std::byte> wrapper{}, cert{}, value{};
    if (!nested(request, 3, wrapper) || !nested(wrapper, 1, cert)
        || !nested(cert, 2, value) || value.size() != key.size()) return false;
    std::copy(value.begin(), value.end(), key.begin());
    return true;
}

// public-only extraction; never reads the callback's private-key regions at +2B8/+298
inline bool from_callback(std::span<const std::byte> callback, std::uint64_t platform,
                          std::uint64_t now, std::span<std::byte> output,
                          std::size_t& written) noexcept {
    written = 0;
    if (callback.size() != 792) return false;
    std::uint32_t result{}, certificateSize{}, signatureSize{};
    std::uint64_t ca{};
    std::memcpy(&result, callback.data(), 4);
    std::memcpy(&certificateSize, callback.data() + 4, 4);
    std::memcpy(&ca, callback.data() + 0x208, 8);
    std::memcpy(&signatureSize, callback.data() + 0x210, 4);
    Metadata metadata{};
    if (result != 1 || certificateSize == 0 || certificateSize > 512 || ca == 0
        || signatureSize != 64
        || !certificate_metadata(callback.subspan(8, certificateSize), platform, now, metadata)) return false;
    protobuf::Writer cert(output);
    if (!cert.write_length_delimited(4, callback.subspan(8, certificateSize))) return false;
    const auto offset = cert.size();
    if (output.size() - offset < 9) return false;
    output[offset] = std::byte{0x29}; // fixed64 field 5
    std::memcpy(output.data() + offset + 1, &ca, 8);
    protobuf::Writer signature(output.subspan(offset + 9));
    if (!signature.write_length_delimited(6, callback.subspan(0x214, signatureSize))) return false;
    written = offset + 9 + signature.size();
    return true;
}
}
