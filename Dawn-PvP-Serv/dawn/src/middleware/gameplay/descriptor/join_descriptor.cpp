#include "join_descriptor.h"

#include <algorithm>
#include <span>

namespace dawn::middleware::gameplay::descriptor {

namespace {

constexpr std::size_t kMachineOffset = 0;
constexpr std::size_t kNetAddrOffset = 8;
constexpr std::size_t kAddressOffset = 0;
// UDP port is low byte first; the IPv4 beside it stays network order
constexpr std::size_t kPortOffset = 4;
// public entry follows five 6-byte local entries and has the same two fields
constexpr std::size_t kPublicAddressOffset = 30;
constexpr std::size_t kPublicPortOffset = 34;
constexpr std::size_t kNatTypeOffset = 40;
// NAT type 1 reads as open; 0 reads as unknown and leaves the address unroutable
constexpr std::byte kNatTypeOpen{1};
constexpr std::size_t kMethodOffset = kNetAddrSize - 1;
// method 0 is the direct path; 6 and 7 would select the relay
constexpr std::byte kDirectMethod{0};
// online session id is at the tail, low half first
constexpr std::size_t kSessionOffset = 110;
constexpr unsigned kByteBits = 8;
constexpr std::uint64_t kByteMask = 0xFF;
// the advertised port must be even
constexpr std::uint16_t kPortAlignment = 2;

void write_network_order(std::span<std::byte> output,
                         std::size_t offset,
                         std::uint64_t value,
                         std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        const unsigned shift = static_cast<unsigned>(width - 1 - index) * kByteBits;
        output[offset + index] = static_cast<std::byte>((value >> shift) & kByteMask);
    }
}

void write_memory_order(std::span<std::byte> output,
                        std::size_t offset,
                        std::uint64_t value,
                        std::size_t width) noexcept {
    for (std::size_t index = 0; index < width; ++index) {
        const unsigned shift = static_cast<unsigned>(index) * kByteBits;
        output[offset + index] = static_cast<std::byte>((value >> shift) & kByteMask);
    }
}

} // namespace

void write_net_addr(std::uint32_t address,
                    std::uint16_t port,
                    std::array<std::byte, kNetAddrSize>& output) noexcept {
    output = {};
    write_network_order(output, kAddressOffset, address, sizeof(std::uint32_t));
    write_memory_order(output, kPortOffset, port, sizeof(std::uint16_t));
    write_network_order(output, kPublicAddressOffset, address, sizeof(std::uint32_t));
    write_memory_order(output, kPublicPortOffset, port, sizeof(std::uint16_t));
    output[kNatTypeOffset] = kNatTypeOpen;
    output[kMethodOffset] = kDirectMethod;
}

bool build(const JoinEndpoint& endpoint, std::array<std::byte, kDescriptorSize>& output) noexcept {
    if (endpoint.machineId == 0 || endpoint.address == 0 || endpoint.port == 0
        || endpoint.port % kPortAlignment != 0 || endpoint.onlineSessionId == 0) {
        return false;
    }
    std::array<std::byte, kNetAddrSize> netAddr{};
    write_net_addr(endpoint.address, endpoint.port, netAddr);

    std::array<std::byte, kDescriptorSize> candidate{};
    write_memory_order(candidate, kMachineOffset, endpoint.machineId, sizeof(std::uint64_t));
    std::copy(netAddr.begin(), netAddr.end(), candidate.begin() + kNetAddrOffset);
    // join key stays zero; the direct path accepts it and carries no key material
    write_memory_order(candidate, kSessionOffset, endpoint.onlineSessionId, sizeof(std::uint64_t));
    output = candidate;
    return true;
}

} // namespace dawn::middleware::gameplay::descriptor
