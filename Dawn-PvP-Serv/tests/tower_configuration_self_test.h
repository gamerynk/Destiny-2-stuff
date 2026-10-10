#pragma once
#include <array>
#include <cstdio>
#include "middleware/bap/matchmaking/configuration_policy.h"
#include "middleware/bap/matchmaking/response/matchmaking_response_encoder.h"
#include "server/bap/encrypted/matchmaking/matchmaking_route.h"

namespace restoration::tests {
inline bool tower_configuration_self_test() noexcept {
    using namespace dawn;
    namespace wire = middleware::bap::matchmaking;
    namespace policy = wire::configuration_policy;
    using middleware::protobuf::Writer;
    using middleware::protobuf::Reader;
    using middleware::protobuf::Field;
    using middleware::protobuf::WireType;
    const auto check = [](bool ok, const char* label) {
        if (!ok) std::printf("FAIL: Tower configuration: %s\n", label);
        return ok;
    };
    const auto request = [](std::array<std::byte,96>& out, std::uint32_t activity,
                            std::uint32_t mm, unsigned variant = 0) {
        std::array<std::byte,48> key{};
        Writer k(key);
        if (variant != 1 && !k.write_varint(1, activity)) return std::size_t{0};
        if (!k.write_varint(2, mm)) return std::size_t{0};
        if (variant == 2 && !k.write_varint(1, activity)) return std::size_t{0};
        if (variant == 3 && !k.write_varint(2, mm)) return std::size_t{0};
        if (variant == 4 && !k.write_varint(1, 0x100000000ULL)) return std::size_t{0};
        if (variant == 5 && !k.write_length_delimited(1, {})) return std::size_t{0};
        if (variant == 8 && !k.write_varint(3, 123)) return std::size_t{0};
        Writer w(out);
        if (!w.write_varint(1,16) || !w.write_varint(2,4) || !w.write_varint(3,123)) return std::size_t{0};
        if (variant != 6 && !w.write_length_delimited(11, std::span(key).first(k.size()))) return std::size_t{0};
        if (variant == 7 && !w.write_length_delimited(11, std::span(key).first(k.size()))) return std::size_t{0};
        return w.size();
    };
    const auto readWait = [](std::span<const std::byte> body, std::uint64_t& wait) {
        wait = 0;
        Reader root(body); Field f{};
        if (!root.next(f) || root.remaining() || f.fieldNumber != 4 || f.wireType != WireType::lengthDelimited) return false;
        Reader cfg(f.bytes);
        while (cfg.remaining()) {
            if (!cfg.next(f)) return false;
            if (f.fieldNumber != 1 || f.wireType != WireType::lengthDelimited) continue;
            Reader lane(f.bytes); Field l{};
            while (lane.remaining()) {
                if (!lane.next(l)) return false;
                if (l.fieldNumber == 3) {
                    return false;
                }
            }
        }
        return true;
    };
    std::array<std::byte,96> body{};
    const auto count = request(body,policy::kTowerActivity,policy::kTowerActivityType);
    if (!check(count && policy::tower(std::span(body).first(count)),"exact Tower key")) return false;
    for (unsigned variant = 1; variant <= 7; ++variant) {
        const auto n = request(body,policy::kTowerActivity,policy::kTowerActivityType,variant);
        if (!check(n && !policy::tower(std::span(body).first(n)),"malformed/duplicate/missing key rejected")) return false;
    }
    for (const auto keys : std::array<std::array<std::uint32_t,2>,5>{{
        {policy::kTowerActivity+1,policy::kTowerActivityType},
        {policy::kTowerActivity,policy::kTowerActivityType+1},{0,0},
        {policy::kTowerActivity,0x8132069DU},
        {policy::kTowerActivity,0x811C9DC5U}}}) { // the empty-hash sentinel is not the type
        const auto n = request(body,keys[0],keys[1]);
        if (!check(n && !policy::tower(std::span(body).first(n)),"other content unchanged")) return false;
    }
    const auto n = request(body,policy::kTowerActivity,policy::kTowerActivityType,8);
    if (!check(policy::tower(std::span(body).first(n)) && !policy::tower(std::span(body).first(n-1)),
               "optional skill and truncated nested message")) return false;

    std::array<std::byte,wire::kMaximumResponseBodySize> output{};
    state::matchmaking::PendingMutation mutation{};
    bool hasMutation = true; std::size_t written = 0; std::uint64_t wait = 0;
    for (const bool tower : {false,true}) {
        const auto size = request(body,policy::kTowerActivity+(tower?0U:1U),policy::kTowerActivityType);
        if (!check(server::bap::encrypted::matchmaking::encode_response({},std::span(body).first(size),
            output,written,mutation,hasMutation) && !hasMutation
            && readWait(std::span(output).first(written),wait)
            && wait == 0U
            && written == 26U,"production configuration omits imposed delay")) return false;
    }
    constexpr unsigned char nativeKeyRequest[] = {
        0x08,0x10,0x10,0x04,0x18,0x7B,0x5A,0x0C,
        0x08,0x9B,0xC8,0xAE,0xC5,0x0E,0x10,0xC8,0xCB,0x80,0xF6,0x05
    };
    const auto nativeBody = std::as_bytes(std::span(nativeKeyRequest));
    if (!check(policy::tower(nativeBody)
        && server::bap::encrypted::matchmaking::encode_response({},nativeBody,
            output,written,mutation,hasMutation) && !hasMutation
        && readWait(std::span(output).first(written),wait) && wait == 0 && written == 26,
        "native-derived Tower request has no imposed composition delay")) return false;
    wire::Response response{};response.kind=wire::RequestKind::configuration;response.towerConfiguration=true;
    for (std::size_t capacity = 0; capacity < 26; ++capacity) {
        output.fill(std::byte{0x77});const auto before=output;written=99;
        if (!check(!wire::response::encode(response,std::span(output).first(capacity),written)
            && written==0 && before==output,"short output atomic")) return false;
    }
    std::puts("PASS: Tower imposed composition delay removed; exact content key, legacy bytes, malformed scope and atomic response");
    std::puts("PASS: native-derived Tower configuration key; authored tag and empty hash rejected");
    return true;
}
} // namespace restoration::tests
