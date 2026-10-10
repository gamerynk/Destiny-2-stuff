#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <limits>
#include "middleware/bap/activity_message/native/activity_clock_authority.h"
#include "middleware/encoding/bit_reader.h"
namespace clock_config = dawn::middleware::bap::activity_message::native::activity_clock;
namespace bits = dawn::middleware::encoding::bits;
void check(bool ok, const char* why) { if (!ok) { std::printf("FAIL: %s\n", why); std::exit(1); } }
int main() {
    std::array<std::byte, 8> bytes{};
    for (std::size_t size=0; size<5; ++size) {
        bytes.fill(std::byte{0xA5}); std::size_t written=99;
        check(!clock_config::encode(clock_config::kRunningCrucible, std::span(bytes).first(size), written)
            && written==0, "undersized clock frame rejected");
        for (auto byte : bytes) check(byte==std::byte{0xA5}, "invalid capacity untouched");
    }
    for (float timing : {-1.0F, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()}) {
        bytes.fill(std::byte{0xA5}); std::size_t written=99;
        check(!clock_config::encode({false,timing}, bytes, written) && written==0, "invalid timing rejected");
        for (auto byte : bytes) check(byte==std::byte{0xA5}, "invalid configuration untouched");
    }
    std::size_t written{};
    check(clock_config::encode(clock_config::kRunningCrucible, bytes, written) && written==5,
        "native configuration encoded in five bytes");
    bits::Reader reader(std::span(bytes).first(written)); std::uint64_t value{};
    check(reader.read(1,value) && value==0, "native reset flag retained");
    check(reader.read(32,value) && value==std::bit_cast<std::uint32_t>(1000.0F/30.0F),
        "native timing interval preserves 30-tick conversion");
    const std::filesystem::path fixturePath{"build/native-pvp-20261002/ui/native-clock-configuration.bin"};
    std::filesystem::create_directories(fixturePath.parent_path());
    std::ofstream fixture(fixturePath,std::ios::binary);
    fixture.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(written));
    check(fixture.good(), "native replay fixture saved");
    std::puts("PASS: native clock configuration wire, interval, invalid values and capacities");
}
