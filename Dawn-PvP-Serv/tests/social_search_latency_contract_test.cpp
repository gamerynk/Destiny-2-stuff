#include <array>
#include <cstdio>
#include <fstream>
#include <string_view>
#include "middleware/bap/matchmaking/social_search.h"

int main(int argc, char** argv) {
    using namespace dawn::middleware;
    if (argc != 2 || std::string_view(argv[1]).empty()) return 2;
    std::array<std::byte, 128> descriptor{};
    for (std::size_t i=0; i<descriptor.size(); ++i) descriptor[i]=std::byte(i);
    std::array<std::byte, 160> output{};
    output.fill(std::byte{0x77}); const auto before=output;
    std::size_t written=9;
    if (bap::matchmaking::social_search::encode(descriptor, std::span(output).first(142), written)
        || written!=0 || output!=before) return 3;
    if (!bap::matchmaking::social_search::encode(descriptor, std::span(output).first(143), written)
        || written!=143) return 4;
    // f3 envelope, repeated f1 result, f1 wrapper, f1 descriptor bytes
    constexpr std::array<unsigned char, 12> prefix{0x1A,0x8C,1,0x0A,0x86,1,0x0A,0x83,1,0x0A,0x80,1};
    for(std::size_t i=0;i<prefix.size();++i) if(output[i]!=std::byte(prefix[i]))return 5;
    for(std::size_t i=0;i<descriptor.size();++i) if(output[i+12]!=descriptor[i])return 6;
    if(output[140]!=std::byte{0x10} || output[141]!=std::byte{0xC8} || output[142]!=std::byte{1})return 7;
    std::ofstream file(argv[1],std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(output.data()),static_cast<std::streamsize>(written));
    if(!file)return 8;
    output=before;written=9;
    if(bap::matchmaking::social_search::encode(std::span(descriptor).first(127),output,written)
        || written!=0 || output!=before)return 9;
    std::puts("PASS: social search latency exact native wire, bounded capacity, untouched descriptor and atomic errors");
    return 0;
}
