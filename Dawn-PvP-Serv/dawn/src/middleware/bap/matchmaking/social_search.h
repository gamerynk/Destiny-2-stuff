#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>
#include "../../protobuf/codec.h"
#include "definition.h"

namespace dawn::middleware::bap::matchmaking::social_search {
// service 42: field 4 is a wire type (0=primary, 1=PUBLIC target), not the native
// activity-client index; field 5 is the activity definition hash
struct Scope { std::uint32_t activity{}; bool primary{}; bool valid{}; };
inline Scope scope(std::span<const std::byte> body) noexcept {
    Scope out{}; bool typeSeen=false,hashSeen=false;
    protobuf::Reader reader(body);protobuf::Field field{};
    while(reader.remaining()) {
        if(!reader.next(field))return {};
        if(field.fieldNumber==4) {
            if(typeSeen || field.wireType!=protobuf::WireType::varint || field.value>1)return {};
            typeSeen=true;out.primary=field.value==0;
        } else if(field.fieldNumber==5) {
            if(hashSeen || field.wireType!=protobuf::WireType::varint || field.value==0 || field.value>0xFFFFFFFFULL)return {};
            hashSeen=true;out.activity=static_cast<std::uint32_t>(field.value);
        }
    }
    out.valid=typeSeen && hashSeen;return out;
}
inline std::uint64_t word(std::span<const std::byte> b,std::size_t offset) noexcept {
    std::uint64_t v{};if(offset<=b.size() && sizeof(v)<=b.size()-offset)std::memcpy(&v,b.data()+offset,sizeof(v));return v;
}
// descriptor layout shared by native group sessions: session id +0, NetAddr +8, secret +94,
// online session +110. Keep all 128 bytes, never reconstruct a key
inline bool steam_descriptor(std::span<const std::byte> b,std::uint64_t platform,
                             std::span<const std::byte> exactAddress={}) noexcept {
    if(b.size()!=kJoinDescriptorSize || platform==0 || word(b,0)==0)return false;
    // PC 86657 A47A86 tests the whole optional +110..127 platform block; when absent, A47DAB
    // -> A46E10 skips Steam JoinLobby. A partly present block with no lobby is not that path
    if(word(b,110)==0 && std::any_of(b.begin()+110,b.end(),
        [](std::byte value){return value!=std::byte{};}))return false;
    const auto method=std::to_integer<unsigned>(b[93]);if(method!=6 && method!=7)return false;
    std::array<char,64> expected{};
    constexpr std::string_view prefix="steamid:";
    std::copy(prefix.begin(),prefix.end(),expected.begin());
    std::array<char,20> digits{};std::size_t n=0;
    do {digits[n++]=static_cast<char>('0'+platform%10);platform/=10;}while(platform && n<digits.size());
    for(std::size_t i=0;i<n;++i)expected[prefix.size()+i]=digits[n-1-i];
    expected[prefix.size()+n]='#';
    if(std::memcmp(b.data()+8,expected.data(),prefix.size()+n+1)!=0)return false;
    return exactAddress.empty() || (exactAddress.size()==86 && std::memcmp(b.data()+8,exactAddress.data(),86)==0);
}
// response.f3.f2 is a varint latency ceiling, not a measured ping. Omitting it gives zero:
// RVA180D2B2 copies it to search+0x50 and RVA1816FFA rejects every probe (reason 12)
inline constexpr std::uint32_t kMaximumSearchLatencyMs = 200;
// PC 86657 nanopb schema: response.f3.results[].f1.f1 = descriptor bytes; optional
// per-entry ping estimates stay absent, the peers measure real QoS
inline bool encode(std::span<const std::byte> descriptor,std::span<std::byte> output,
                   std::size_t& written) noexcept {
    written=0;if(descriptor.size()!=kJoinDescriptorSize)return false;
    std::array<std::byte,160> wrapper{},entry{},results{},body{};
    protobuf::Writer w(wrapper);if(!w.write_length_delimited(1,descriptor))return false;
    protobuf::Writer e(entry);if(!e.write_length_delimited(1,std::span(wrapper).first(w.size())))return false;
    protobuf::Writer r(results);
    if(!r.write_length_delimited(1,std::span(entry).first(e.size()))
       || !r.write_varint(2,kMaximumSearchLatencyMs))return false;
    protobuf::Writer b(body);if(!b.write_length_delimited(3,std::span(results).first(r.size())) || output.size()<b.size())return false;
    std::copy_n(body.begin(),b.size(),output.begin());written=b.size();return true;
}
}
