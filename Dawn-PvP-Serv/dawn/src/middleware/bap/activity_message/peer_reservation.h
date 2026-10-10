#pragma once
#include <array>
#include <bit>
#include "../../encoding/bit_reader.h"
#include "activity_client_identity_parser.h"

namespace dawn::middleware::bap::activity_message::peer_reservation {
inline constexpr std::uint32_t kRequestType = 13;
inline constexpr std::uint32_t kReleaseType = 14;
inline constexpr std::size_t kCapacity = 32;
struct Request {
    std::uint32_t epoch{};
    std::array<client_identity::ClientIdentity, kCapacity> peers{};
    std::size_t count{};
};
struct Release { std::uint32_t epoch{}, reason{}; std::uint64_t key{}; };

// 80809F7B is eight byte elements, low byte first (also used for identity field3)
inline bool read_key(encoding::bits::Reader& reader, std::uint64_t& key) noexcept {
    key = 0;
    for (unsigned i=0; i<8; ++i) {
        std::uint64_t byte{};
        if (!reader.read(8, byte)) return false;
        key |= byte << (8*i);
    }
    return true;
}
// shared 808086BB member block: startup field7 and type13 have identical unaligned rows
inline bool read_peers(encoding::bits::Reader& reader, Request& output) noexcept {
    Request parsed{};
    std::uint64_t count{};
    if (!reader.read(6,count) || count>kCapacity) return false;
    parsed.count=static_cast<std::size_t>(count);
    for (std::size_t i=0; i<parsed.count; ++i) {
        auto& peer=parsed.peers[i];
        std::uint64_t field1{}, field2{};
        if (!read_key(reader,peer.memberKey) || !reader.read(10,field1) || !reader.read(32,field2)
            || !read_key(reader,peer.field3) || !reader.read(64,peer.accountSoid)
            || !reader.read(64,peer.field5) || !reader.read(64,peer.field6)) return false;
        peer.field1=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(field1)-1U);
        peer.field2=std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(field2)-0x80000000U);
    }
    output=parsed;
    return true;
}
inline bool parse(std::span<const std::byte> body, Request& output) noexcept {
    output = {};
    encoding::bits::Reader reader(body);
    Request parsed{};
    std::uint64_t epoch{};
    if (!reader.read(32,epoch) || !read_peers(reader,parsed)) return false;
    parsed.epoch=static_cast<std::uint32_t>(epoch);
    if (reader.remaining_bits()>7) return false;
    output=parsed;
    return true;
}
inline bool parse(std::span<const std::byte> body, Release& output) noexcept {
    output={};
    if (body.size()!=16) return false;
    encoding::bits::Reader reader(body);
    std::uint64_t epoch{},reason{},key{};
    if (!reader.read(32,epoch)||!reader.read(32,reason)||!read_key(reader,key)) return false;
    output={static_cast<std::uint32_t>(epoch),static_cast<std::uint32_t>(reason),key};
    return true;
}
}
