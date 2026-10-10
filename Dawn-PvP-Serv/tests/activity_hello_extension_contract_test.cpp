#include <array>
#include <cstdio>
#include "server/bap/server_hello_shape.h"
#include "middleware/protobuf/codec.h"

namespace shape=dawn::server::bap::hello_shape;
namespace proto=dawn::middleware::protobuf;
bool check(bool ok,const char* label) {if(!ok) std::printf("FAIL: %s\n",label);return ok;}
int main() {
    std::array<std::byte,32> token{};
    token.fill(std::byte{0xA5});
    std::array<std::byte,80> bytes{};
    proto::Writer w(bytes);
    if(!check(w.write_length_delimited(1,token) && w.write_varint(2,1)
        && w.size()==36 && shape::valid(std::span(bytes).first(36)),"primary baseline")) return 1;
    bytes[35]=std::byte{2};
    if(!check(shape::valid(std::span(bytes).first(36)),"activity owner baseline")) return 1;
    if(!check(w.write_varint(3,0x9EAA00010020000BULL) && w.size()==47,
        "optional uint64 fixture size")) return 1;
    bytes[35]=std::byte{3};
    if(!check(shape::valid(std::span(bytes).first(47)),"gameplay child baseline")) return 1;
    bytes[35]=std::byte{2};
    if(!check(shape::valid(std::span(bytes).first(47)),"native follower v2 extended hello")) return 1;
    const auto good=bytes;
    for(std::size_t n=0;n<=80;++n) {
        if(n==36 || n==47) continue;
        if(!check(!shape::valid(std::span(bytes).first(n)),"unexpected length rejected")) return 1;
    }
    for(unsigned v: {0U,1U,4U,255U}) {
        bytes=good; bytes[35]=std::byte(v);
        if(!check(!shape::valid(std::span(bytes).first(47)),"unobserved extended version rejected")) return 1;
    }
    for(std::size_t i: {0U,1U,34U,36U}) {
        bytes=good; bytes[i]^=std::byte{0x01};
        if(!check(!shape::valid(std::span(bytes).first(47)),"wrong field header rejected")) return 1;
    }
    bytes=good; bytes[46]=std::byte{0x02};
    if(!check(!shape::valid(std::span(bytes).first(47)),"uint64 overflow rejected")) return 1;
    bytes=good; bytes[40]&=std::byte{0x7F};
    if(!check(!shape::valid(std::span(bytes).first(47)),"early termination and trailing data rejected")) return 1;
    bytes=good; bytes[46]=std::byte{0x81};
    if(!check(!shape::valid(std::span(bytes).first(47)),"unterminated field rejected")) return 1;
    bytes=good; bytes[36]=std::byte{0x10};
    if(!check(!shape::valid(std::span(bytes).first(47)),"duplicate version field rejected")) return 1;
    std::puts("PASS: v1/v2/v3 baselines, follower v2 field3, malformed lengths/tags/overflow/trailing data");
}
