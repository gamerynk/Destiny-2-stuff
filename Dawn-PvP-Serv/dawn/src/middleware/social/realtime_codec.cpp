#include "realtime_codec.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace dawn::middleware::social::realtime {
namespace {

constexpr std::uint32_t kRequestMagic = 0x31515352U;
constexpr std::uint32_t kResponseMagic = 0x31525352U;
constexpr std::uint16_t kVersion = 1;

template <typename Value, bool = std::is_enum_v<Value>> struct IntegerBase final {
    using Type = Value;
};

template <typename Value> struct IntegerBase<Value, true> final {
    using Type = std::underlying_type_t<Value>;
};

class Writer final {
public:
    explicit Writer(std::span<std::byte> output) noexcept : output_(output) {}

    template <typename Value>
    [[nodiscard]] bool integer(Value value) noexcept {
        static_assert(std::is_integral_v<Value> || std::is_enum_v<Value>);
        using Source = typename IntegerBase<Value>::Type;
        using Unsigned = std::make_unsigned_t<Source>;
        if (offset_ > output_.size() || sizeof(Source) > output_.size() - offset_) {
            return false;
        }
        Unsigned bits = static_cast<Unsigned>(static_cast<Source>(value));
        for (std::size_t index = 0; index < sizeof(Source); ++index) {
            output_[offset_++] = static_cast<std::byte>(bits & static_cast<Unsigned>(0xFFU));
            bits >>= 8U;
        }
        return true;
    }

    [[nodiscard]] bool bytes(std::span<const std::byte> value) noexcept {
        if (offset_ > output_.size() || value.size() > output_.size() - offset_) {
            return false;
        }
        std::copy(value.begin(), value.end(), output_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ += value.size();
        return true;
    }

    [[nodiscard]] std::size_t size() const noexcept { return offset_; }

private:
    std::span<std::byte> output_;
    std::size_t offset_{};
};

class Reader final {
public:
    explicit Reader(std::span<const std::byte> input) noexcept : input_(input) {}

    template <typename Value>
    [[nodiscard]] bool integer(Value& value) noexcept {
        static_assert(std::is_integral_v<Value> || std::is_enum_v<Value>);
        using Destination = typename IntegerBase<Value>::Type;
        using Unsigned = std::make_unsigned_t<Destination>;
        if (offset_ > input_.size() || sizeof(Destination) > input_.size() - offset_) {
            return false;
        }
        Unsigned bits = 0;
        for (std::size_t index = 0; index < sizeof(Destination); ++index) {
            bits |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(input_[offset_++]))
                    << static_cast<unsigned>(index * 8U);
        }
        if constexpr (std::is_enum_v<Value>) {
            value = static_cast<Value>(static_cast<Destination>(bits));
        } else {
            value = static_cast<Value>(bits);
        }
        return true;
    }

    [[nodiscard]] bool bytes(std::span<std::byte> value) noexcept {
        if (offset_ > input_.size() || value.size() > input_.size() - offset_) {
            return false;
        }
        std::copy_n(input_.begin() + static_cast<std::ptrdiff_t>(offset_),
                    value.size(), value.begin());
        offset_ += value.size();
        return true;
    }

    [[nodiscard]] bool finished() const noexcept { return offset_ == input_.size(); }

private:
    std::span<const std::byte> input_;
    std::size_t offset_{};
};

template <std::size_t Capacity>
[[nodiscard]] bool valid_text(const std::array<char, Capacity>& text, bool allowEmpty) noexcept {
    const auto end = std::find(text.cbegin(), text.cend(), '\0');
    if (end == text.cend() || (!allowEmpty && end == text.cbegin())) {
        return false;
    }
    return std::all_of(text.cbegin(), end, [](char value) noexcept {
        const auto byte = static_cast<unsigned char>(value);
        return byte >= 0x20U && byte <= 0x7EU;
    });
}

template <std::size_t Capacity>
[[nodiscard]] bool write_text(Writer& writer,
                              const std::array<char, Capacity>& text,
                              bool allowEmpty) noexcept {
    if (!valid_text(text, allowEmpty)) {
        return false;
    }
    const auto end = std::find(text.cbegin(), text.cend(), '\0');
    const std::size_t size = static_cast<std::size_t>(end - text.cbegin());
    return size <= (std::numeric_limits<std::uint16_t>::max)()
           && writer.integer(static_cast<std::uint16_t>(size))
           && writer.bytes({reinterpret_cast<const std::byte*>(text.data()), size});
}

template <std::size_t Capacity>
[[nodiscard]] bool read_text(Reader& reader,
                             std::array<char, Capacity>& text,
                             bool allowEmpty) noexcept {
    std::uint16_t size = 0;
    if (!reader.integer(size) || size >= Capacity || (!allowEmpty && size == 0)) {
        return false;
    }
    text = {};
    return reader.bytes({reinterpret_cast<std::byte*>(text.data()), size});
}

[[nodiscard]] bool write_metadata(Writer& writer, const Metadata& metadata) noexcept {
    return write_text(writer, metadata.key, false)
           && write_text(writer, metadata.value, true);
}

[[nodiscard]] bool read_metadata(Reader& reader, Metadata& metadata) noexcept {
    return read_text(reader, metadata.key, false)
           && read_text(reader, metadata.value, true);
}

[[nodiscard]] bool write_lobby(Writer& writer, const Lobby& lobby) noexcept {
    if (lobby.metadataCount > lobby.metadata.size() || lobby.memberCount > lobby.members.size()
        || lobby.maximumMembers < 0
        || lobby.maximumMembers > static_cast<std::int32_t>(kMemberCapacity)) {
        return false;
    }
    if (!writer.integer(lobby.id) || !writer.integer(lobby.ownerPlatformId)
        || !writer.integer(lobby.revision) || !writer.integer(lobby.type)
        || !writer.integer(lobby.maximumMembers)
        || !writer.integer(static_cast<std::uint8_t>(lobby.joinable ? 1U : 0U))
        || !writer.integer(lobby.gameServerPlatformId) || !writer.integer(lobby.gameServerIp)
        || !writer.integer(lobby.gameServerPort)
        || !writer.integer(static_cast<std::uint16_t>(lobby.metadataCount))) {
        return false;
    }
    for (std::size_t index = 0; index < lobby.metadataCount; ++index) {
        if (!write_metadata(writer, lobby.metadata[index])) {
            return false;
        }
    }
    if (!writer.integer(static_cast<std::uint16_t>(lobby.memberCount))) {
        return false;
    }
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        const Member& member = lobby.members[index];
        if (member.platformId == 0 || member.accountSoid == 0
            || member.metadataCount > member.metadata.size()
            || !writer.integer(member.platformId) || !writer.integer(member.accountSoid)
            || !writer.integer(static_cast<std::uint16_t>(member.metadataCount))) {
            return false;
        }
        for (std::size_t metadataIndex = 0; metadataIndex < member.metadataCount;
             ++metadataIndex) {
            if (!write_metadata(writer, member.metadata[metadataIndex])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool read_lobby(Reader& reader, Lobby& lobby) noexcept {
    lobby = {};
    std::uint8_t joinable = 0;
    std::uint16_t metadataCount = 0;
    if (!reader.integer(lobby.id) || !reader.integer(lobby.ownerPlatformId)
        || !reader.integer(lobby.revision) || !reader.integer(lobby.type)
        || !reader.integer(lobby.maximumMembers) || !reader.integer(joinable)
        || !reader.integer(lobby.gameServerPlatformId) || !reader.integer(lobby.gameServerIp)
        || !reader.integer(lobby.gameServerPort)
        || !reader.integer(metadataCount) || joinable > 1U
        || metadataCount > lobby.metadata.size() || lobby.maximumMembers < 0
        || lobby.maximumMembers > static_cast<std::int32_t>(kMemberCapacity)) {
        return false;
    }
    lobby.joinable = joinable != 0;
    lobby.metadataCount = metadataCount;
    for (std::size_t index = 0; index < lobby.metadataCount; ++index) {
        if (!read_metadata(reader, lobby.metadata[index])) {
            return false;
        }
    }
    std::uint16_t memberCount = 0;
    if (!reader.integer(memberCount) || memberCount > lobby.members.size()) {
        return false;
    }
    lobby.memberCount = memberCount;
    for (std::size_t index = 0; index < lobby.memberCount; ++index) {
        Member& member = lobby.members[index];
        std::uint16_t count = 0;
        if (!reader.integer(member.platformId) || !reader.integer(member.accountSoid)
            || !reader.integer(count) || member.platformId == 0 || member.accountSoid == 0
            || count > member.metadata.size()) {
            return false;
        }
        member.metadataCount = count;
        for (std::size_t metadataIndex = 0; metadataIndex < member.metadataCount;
             ++metadataIndex) {
            if (!read_metadata(reader, member.metadata[metadataIndex])) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool encode_request(const Request& request,
                    std::span<std::byte> output,
                    std::size_t& written) noexcept {
    written = 0;
    if (request.payloadSize > request.payload.size()) {
        return false;
    }
    Writer writer(output);
    if (!writer.integer(kRequestMagic) || !writer.integer(kVersion)
        || !writer.integer(request.operation) || !writer.integer(request.cursor)
        || !writer.integer(request.lobbyId) || !writer.integer(request.targetPlatformId)
        || !writer.integer(request.valueA) || !writer.integer(request.valueB)
        || !write_text(writer, request.key, true) || !write_text(writer, request.value, true)
        || !writer.integer(static_cast<std::uint32_t>(request.payloadSize))
        || !writer.bytes(std::span(request.payload).first(request.payloadSize))) {
        return false;
    }
    written = writer.size();
    return true;
}

bool decode_request(std::span<const std::byte> input, Request& request) noexcept {
    request = {};
    Reader reader(input);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint32_t payloadSize = 0;
    if (!reader.integer(magic) || !reader.integer(version) || magic != kRequestMagic
        || version != kVersion || !reader.integer(request.operation)
        || !reader.integer(request.cursor) || !reader.integer(request.lobbyId)
        || !reader.integer(request.targetPlatformId) || !reader.integer(request.valueA)
        || !reader.integer(request.valueB)
        || !read_text(reader, request.key, true)
        || !read_text(reader, request.value, true) || !reader.integer(payloadSize)
        || payloadSize > request.payload.size()) {
        request = {};
        return false;
    }
    request.payloadSize = payloadSize;
    if (!reader.bytes(std::span(request.payload).first(request.payloadSize))
        || !reader.finished()) {
        request = {};
        return false;
    }
    return request.operation >= Operation::poll
           && request.operation <= Operation::sendRendezvousBatch;
}

bool encode_response(const Response& response,
                     std::span<std::byte> output,
                     std::size_t& written) noexcept {
    written = 0;
    if (response.eventCount > response.events.size()) {
        return false;
    }
    Writer writer(output);
    if (!writer.integer(kResponseMagic) || !writer.integer(kVersion)
        || !writer.integer(static_cast<std::uint16_t>(0)) || !writer.integer(response.result)
        || !writer.integer(response.cursor) || !write_lobby(writer, response.lobby)
        || !writer.integer(static_cast<std::uint16_t>(response.eventCount))) {
        return false;
    }
    for (std::size_t index = 0; index < response.eventCount; ++index) {
        const Event& event = response.events[index];
        if (event.sequence == 0 || event.payloadSize > event.payload.size()
            || !writer.integer(event.sequence) || !writer.integer(event.kind)
            || !writer.integer(event.lobbyId) || !writer.integer(event.sourcePlatformId)
            || !writer.integer(event.memberPlatformId)
            || !writer.integer(static_cast<std::uint32_t>(event.payloadSize))
            || !writer.bytes(std::span(event.payload).first(event.payloadSize))) {
            return false;
        }
    }
    written = writer.size();
    return true;
}

bool decode_response(std::span<const std::byte> input, Response& response) noexcept {
    response = {};
    Reader reader(input);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    std::uint16_t eventCount = 0;
    if (!reader.integer(magic) || !reader.integer(version) || !reader.integer(reserved)
        || magic != kResponseMagic || version != kVersion || reserved != 0
        || !reader.integer(response.result) || !reader.integer(response.cursor)
        || !read_lobby(reader, response.lobby) || !reader.integer(eventCount)
        || eventCount > response.events.size()) {
        response = {};
        return false;
    }
    response.eventCount = eventCount;
    for (std::size_t index = 0; index < response.eventCount; ++index) {
        Event& event = response.events[index];
        std::uint32_t payloadSize = 0;
        if (!reader.integer(event.sequence) || !reader.integer(event.kind)
            || !reader.integer(event.lobbyId) || !reader.integer(event.sourcePlatformId)
            || !reader.integer(event.memberPlatformId) || !reader.integer(payloadSize)
            || event.sequence == 0 || payloadSize > event.payload.size()) {
            response = {};
            return false;
        }
        event.payloadSize = payloadSize;
        if (!reader.bytes(std::span(event.payload).first(event.payloadSize))) {
            response = {};
            return false;
        }
    }
    if (!reader.finished()) {
        response = {};
        return false;
    }
    return true;
}

} // namespace dawn::middleware::social::realtime
