#include <array>
#include <cstdio>
#include <cstring>
#include "middleware/bap/account_translation/account_translation_response.h"
#include "middleware/encoding/byte_order.h"
namespace tr=dawn::middleware::bap::account_translation;
namespace enc=dawn::middleware::encoding;
int main() {
    const auto resolver=+[](void*,std::uint64_t id) noexcept -> std::uint64_t {
        if(id==0x0110000101020304ULL) return 0x9EAA000100200100ULL;
        if(id==0x0110000105060708ULL) return 0x9EAA000100E00100ULL;
        return 0;
    };
    // independent native svc-24 layout: count, types, source array, result array; an interleaved
    // pair encoder puts a Dawn ID at byte 12 and must fail
    constexpr unsigned char golden[]{
        0x00,0x02,0x0C,0xFF,
        0x01,0x10,0x00,0x01,0x01,0x02,0x03,0x04,
        0x01,0x10,0x00,0x01,0x05,0x06,0x07,0x08,
        0x9E,0xAA,0x00,0x01,0x00,0x20,0x01,0x00,
        0x9E,0xAA,0x00,0x01,0x00,0xE0,0x01,0x00};
    std::array<std::byte,20> request{};
    enc::write_u16_be(std::span(request).first<2>(),2);
    request[2]=std::byte{12};request[3]=std::byte{255};
    enc::write_u64_be(std::span(request).subspan<4,8>(),0x0110000101020304ULL);
    enc::write_u64_be(std::span(request).subspan<12,8>(),0x0110000105060708ULL);
    std::array<std::byte,36> answer{};std::size_t written{};
    if(!tr::encode_response(request,nullptr,resolver,answer,written)
        || written!=sizeof(golden) || std::memcmp(answer.data(),golden,sizeof(golden))) {
        std::puts("FAIL: native two-array account-translation fixture");return 1;
    }
    enc::write_u64_be(std::span(request).subspan<4,8>(),0x01100001FFFFFFFFULL);
    if(!tr::encode_response(request,nullptr,resolver,answer,written) || written!=20
        || enc::read_u16_be(std::span(answer).first<2>())!=1
        || enc::read_u64_be(std::span(answer).subspan<4,8>())!=0x0110000105060708ULL
        || enc::read_u64_be(std::span(answer).subspan<12,8>())!=0x9EAA000100E00100ULL) {
        std::puts("FAIL: unresolved batch entries compact both arrays");return 1;
    }
    std::puts("PASS: native account translation array order and unresolved-row compaction");return 0;
}
