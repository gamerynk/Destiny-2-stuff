#include <array>
#include <cstdio>
#include <vector>
#include "middleware/web_service/web_service_envelope.h"
#include "middleware/encoding/bit_reader.h"
#include "middleware/queuez/queuez_update.h"
int main(int argc, char** argv) {
    namespace ws = dawn::middleware::web_service;
    namespace qz = dawn::middleware::queuez;
    std::array<std::byte,256> raw{}, response{};
    const std::array families{qz::Family{0,0x9EAA000100200100ULL,0,qz::kFullSnapshotFlag,{}}};
    std::size_t rawSize{}, size{};
    if (!qz::encode_update(families,raw,rawSize) || rawSize != 25) return 1;
    const ws::Message request{206,0x12345678,{}};
    if (!ws::encode_response(request,ws::ResponseShape::statusOnly,{},response,size,std::span(raw).first(rawSize))) return 2;
    if (argc > 1) { FILE* file{}; if (fopen_s(&file,argv[1],"wb") != 0 || !file) return 10; const auto n=fwrite(response.data(),1,size,file); fclose(file); if(n!=size)return 11; }
    ws::Message parsed{};
    if (!ws::parse_request(std::span(response).first(size),parsed) || parsed.opcode != 206 || parsed.transactionId != request.transactionId) return 3;
    dawn::middleware::encoding::bits::Reader reader(parsed.payload);
    std::uint64_t field{};
    if (!reader.read(5,field) || field != 1) return 12;
    if (!reader.read(1,field) || field != 1 || !reader.read(16,field) || field != rawSize) return 4;
    for (std::size_t i=0;i<rawSize;++i) if (!reader.read(8,field) || field != std::to_integer<unsigned>(raw[i])) return 5;
    if (!reader.read(1,field) || field != 0) return 6;
    for (std::size_t capacity=0;capacity<size;++capacity) {
        std::size_t shortSize=99;
        if (ws::encode_response(request,ws::ResponseShape::statusOnly,{},std::span(response).first(capacity),shortSize,std::span(raw).first(rawSize)) || shortSize != 0) return 7;
    }
    if (!ws::encode_response(request,ws::ResponseShape::statusOnly,{},response,size) || size != 7 || response[6] != std::byte{8}) return 8;
    std::vector<std::byte> oversized(65536);
    if (ws::encode_response(request,ws::ResponseShape::statusOnly,{},response,size,oversized) || size != 0) return 9;
    std::puts("PASS: same real25-byte family update in native svc11 optional field, exact correlation, short capacities, absent compatibility, unsigned16 bound");
}
