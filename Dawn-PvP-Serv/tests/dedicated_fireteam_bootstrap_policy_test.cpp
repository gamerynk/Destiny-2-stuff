#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "client/hooks/bootflow/dedicated_fireteam_bootstrap_policy.h"

namespace {

namespace policy = dawn::client::hooks::bootflow::dedicated_fireteam_bootstrap_policy;
namespace descriptor = dawn::middleware::gameplay::descriptor;

constexpr std::uint64_t kLobby = 0x0184000000000042ULL;

[[nodiscard]] bool require(bool condition, const char* name) noexcept {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
    }
    return condition;
}

template <typename Value>
void write_little(std::array<std::byte, descriptor::kDescriptorSize>& output,
                  std::size_t offset,
                  Value value) noexcept {
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        output[offset + index] =
            static_cast<std::byte>((value >> (index * 8U)) & static_cast<Value>(0xFFU));
    }
}

template <typename Value>
void write_network(std::array<std::byte, descriptor::kDescriptorSize>& output,
                   std::size_t offset,
                   Value value) noexcept {
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        const std::size_t shift = (sizeof(Value) - 1U - index) * 8U;
        output[offset + index] =
            static_cast<std::byte>((value >> shift) & static_cast<Value>(0xFFU));
    }
}

[[nodiscard]] std::array<std::byte, descriptor::kDescriptorSize> valid_descriptor() noexcept {
    std::array<std::byte, descriptor::kDescriptorSize> result{};
    constexpr std::uint64_t machine = 0xC59EAA0001001001ULL;
    constexpr std::uint32_t address = 0xCB00712AU;
    constexpr std::uint16_t port = 30976U;
    write_little(result, 0, machine);
    write_network(result, 8, address);
    write_little(result, 12, port);
    write_network(result, 38, address);
    write_little(result, 42, port);
    result[48] = std::byte{1};
    result[93] = std::byte{0};
    write_little(result, 110, kLobby);
    return result;
}

} // namespace

int main() {
    bool passed = true;
    std::array<std::byte, policy::kPrimaryAccessorSignatureSize> accessor{
        std::byte{0x40}, std::byte{0x53}, std::byte{0x48}, std::byte{0x83},
        std::byte{0xEC}, std::byte{0x20}, std::byte{0x48}, std::byte{0x63},
        std::byte{0xD9}, std::byte{0xE8}, std::byte{0x11}, std::byte{0x22},
        std::byte{0x33}, std::byte{0x44}, std::byte{0x48}, std::byte{0x85},
        std::byte{0xC0}, std::byte{0x74}, std::byte{0x09}, std::byte{0x83},
        std::byte{0xFB}, std::byte{0xFF}, std::byte{0x74}, std::byte{0x07},
        std::byte{0x4C}, std::byte{0x69}, std::byte{0xC3}, std::byte{0x48},
        std::byte{0x91}, std::byte{0x03}, std::byte{0x00}, std::byte{0x4C},
        std::byte{0x03}, std::byte{0xC0}, std::byte{0x49}, std::byte{0x63},
        std::byte{0x40}, std::byte{0x10}, std::byte{0x48}, std::byte{0x69},
        std::byte{0xC8}, std::byte{0xA0}, std::byte{0xC8}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x49}, std::byte{0x8D}, std::byte{0x40},
        std::byte{0x18}, std::byte{0x48}, std::byte{0x03}, std::byte{0xC1},
        std::byte{0x48}, std::byte{0x83}, std::byte{0xC4}, std::byte{0x20},
    };
    passed &= require(policy::primary_accessor_is_pc_1_59(accessor),
                      "exact C26490 accessor signature accepted");
    accessor[10] ^= std::byte{0xFF};
    accessor[18] ^= std::byte{0xFF};
    accessor[23] ^= std::byte{0xFF};
    accessor[37] ^= std::byte{0xFF};
    accessor[48] ^= std::byte{0xFF};
    passed &= require(policy::primary_accessor_is_pc_1_59(accessor),
                      "only C26490 relocations and structure displacements wildcarded");
    accessor[36] ^= std::byte{1};
    passed &= require(!policy::primary_accessor_is_pc_1_59(accessor),
                      "C26490 opcode mutation rejected");

    const auto descriptor = valid_descriptor();
    passed &= require(policy::descriptor_is_native_owner(descriptor, kLobby),
                      "exact retail owner descriptor accepted");
    auto relayDescriptor = descriptor;
    relayDescriptor[93] = std::byte{7};
    passed &= require(policy::descriptor_is_native_owner(relayDescriptor, kLobby),
                      "retail owner relay transport is preserved");
    relayDescriptor[93] = std::byte{8};
    passed &= require(!policy::descriptor_is_native_owner(relayDescriptor, kLobby),
                      "unknown retail transport is rejected");

    auto malformed = descriptor;
    malformed.fill(std::byte{});
    passed &= require(!policy::descriptor_is_native_owner(malformed, kLobby),
                      "empty native owner descriptor rejected");
    malformed = descriptor;
    malformed[0] = std::byte{};
    malformed[1] = std::byte{};
    malformed[2] = std::byte{};
    malformed[3] = std::byte{};
    malformed[4] = std::byte{};
    malformed[5] = std::byte{};
    malformed[6] = std::byte{};
    malformed[7] = std::byte{};
    passed &= require(!policy::descriptor_is_native_owner(malformed, kLobby),
                      "zero native owner machine rejected");
    passed &= require(!policy::descriptor_is_native_owner(descriptor, kLobby + 1U),
                      "native owner descriptor from another lobby rejected");

    if (passed) {
        std::puts("PASS: exact native PRIMARY accessor and retail owner descriptor validation");
        return 0;
    }
    return 1;
}
