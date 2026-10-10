#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "client/hooks/egress/platform/sdk.h"
#include "native_route_data_fixture.h"
#include "middleware/protobuf/codec.h"
#include "middleware/signon/response.h"
#include "middleware/bap/activity_host/activity_host_response.h"
#include "middleware/bap/activity_host_manager/response/activity_manager_response.h"
#include "middleware/encoding/byte_order.h"
#include "middleware/encoding/bit_raw.h"
#include "server/gameplay/group/activity_host_endpoint.h"
#include "state/session/native_bap_endpoint.h"
#include "state/session/session_runtime.h"
#include "client/network/native_routes/runtime.h"
#include "client/network/native_routes/contract.h"

namespace dawn::core::settings {
Settings g_producerSettings{};
const Settings& get() noexcept { return g_producerSettings; }
}

namespace {
bool expect(bool value, const char* label) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", label);
    return value;
}

unsigned g_dnsCalls{};
hostent* WSAAPI fake_dns(const char*) {
    ++g_dnsCalls;
    static in_addr address{};
    address.s_addr = htonl(0xC6336407U);
    static char* addresses[]{reinterpret_cast<char*>(&address), nullptr};
    static hostent entry{};
    entry.h_addrtype = AF_INET;
    entry.h_length = 4;
    entry.h_addr_list = addresses;
    return &entry;
}

bool replay_lookup(const std::vector<char>& original, const std::vector<char>& changed,
                   const dawn::client::hooks::egress::native_routes::Plan& plan,
                   const char* capturedText) {
    std::ifstream input(capturedText, std::ios::binary);
    if (!input) return expect(false, "captured native text opens read-only");
    const std::vector<char> text(std::istreambuf_iterator<char>(input), {});
    constexpr std::size_t kImageSize = 0x3CD0000;
    auto* memory = static_cast<char*>(VirtualAlloc(nullptr, kImageSize,
                                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (memory == nullptr) return expect(false, "isolated replay image allocates");
    if (text.size() + 0x1000 > 0x1B91000 || changed.size() > kImageSize - 0x1B91000) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return expect(false, "captured replay sections fit expected layout");
    }
    std::memcpy(memory + 0x1000, text.data(), text.size());
    std::memcpy(memory + 0x1B91000, changed.data(), changed.size());
    *reinterpret_cast<void**>(memory + 0x3CC8138) = reinterpret_cast<void*>(&::isalpha);
    *reinterpret_cast<void**>(memory + 0x1B91950) = reinterpret_cast<void*>(&::inet_addr);
    *reinterpret_cast<void**>(memory + 0x1B91908) = reinterpret_cast<void*>(&fake_dns);
    DWORD previous{};
    const bool protectedImage = VirtualProtect(memory, kImageSize, PAGE_EXECUTE_READ, &previous)
                                != FALSE;
    bool passed = expect(protectedImage, "isolated replay image seals after supplying imports");
    if (protectedImage) {
        FlushInstructionCache(GetCurrentProcess(), memory, kImageSize);
        using Lookup = std::uint64_t(__fastcall*)(const char*, void*, unsigned, unsigned);
        const auto lookup = reinterpret_cast<Lookup>(memory + 0x1A91770);
        std::array<std::byte, 0x84 * 4> results{};
        const unsigned before = g_dnsCalls;
        passed &= expect(lookup(original.data() + plan.offsets.front(), results.data(), 4, 0) == 1
                             && g_dnsCalls == before + 1,
                         "original captured STUN name takes native DNS branch");
        const unsigned afterOriginal = g_dnsCalls;
        for (const auto offset : plan.offsets) {
            results.fill(std::byte{});
            const auto found = lookup(memory + 0x1B91000 + offset, results.data(), 4, 0);
            std::uint32_t address{};
            std::memcpy(&address, results.data(), sizeof(address));
            passed &= expect(found == 1 && ntohl(address) == 0xCB007113U
                                 && g_dnsCalls == afterOriginal,
                             "captured native lookup uses configured literal without DNS hook");
        }
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    return passed;
}

bool wire_endpoint(const dawn::state::SignOnState& state, const char* expectedAddress) {
    namespace protobuf = dawn::middleware::protobuf;
    std::array<std::byte, 2048> output{};
    std::size_t written{};
    if (!dawn::middleware::signon::encode_success(state, {}, 3600, 1000, output, written))
        return false;
    protobuf::Reader response(std::span(output).first(written));
    protobuf::Field field{};
    bool outerAddress = false;
    bool relayAddress = false;
    bool relayPort = false;
    bool nativeAddress = false;
    unsigned urls = 0;
    while (response.next(field)) {
        if (field.fieldNumber == 14) outerAddress = field.value == state.relayAddress;
        if (field.fieldNumber != 2) continue;
        protobuf::Reader success(field.bytes);
        protobuf::Field inner{};
        while (success.next(inner)) {
            if (inner.fieldNumber == 6) {
                relayAddress = inner.value == state.relayAddress;
                // the retail consumer copies this decoded scalar into sin_addr as-is, so test with the real
                // IPv4 consumer rather than the encoder's own integer convention
                in_addr address{};
                address.s_addr = static_cast<ULONG>(inner.value);
                std::array<char, INET_ADDRSTRLEN> text{};
                nativeAddress = InetNtopA(AF_INET, &address, text.data(), text.size()) != nullptr
                    && std::strcmp(text.data(), expectedAddress) == 0;
            }
            if (inner.fieldNumber == 7) relayPort = inner.value == state.relayPort;
            if (inner.fieldNumber >= 14 && inner.fieldNumber <= 16) {
                const std::string url(reinterpret_cast<const char*>(inner.bytes.data()), inner.bytes.size());
                if (url.starts_with(std::string("http://") + expectedAddress + "/cfg_")) ++urls;
            }
        }
    }
    bool passed = outerAddress && relayAddress && relayPort && nativeAddress && urls == 3
        && response.remaining() == 0;
    namespace encoding = dawn::middleware::encoding;
    constexpr std::uint64_t host = 0x0102030405060708ULL;
    std::array<std::byte, 8> hostRequest{};
    encoding::write_u64_be(hostRequest, host);
    std::array<std::byte, 16> fah{};
    passed &= dawn::middleware::bap::activity_host::encode_response(
        hostRequest, state.relayAddress, state.relayPort, fah, written) && written == fah.size();
    in_addr address{};
    // svc17's address consumer copies these four octets without a byte swap
    std::memcpy(&address.s_addr, fah.data() + 8, sizeof(address.s_addr));
    std::array<char, INET_ADDRSTRLEN> text{};
    passed &= InetNtopA(AF_INET, &address, text.data(), text.size()) != nullptr
        && std::strcmp(text.data(), expectedAddress) == 0
        && encoding::read_u64_be(std::span(fah).first<8>()) == host
        && encoding::read_u16_be(std::span(fah).subspan<12, 2>()) == 0
        && encoding::read_u16_be(std::span(fah).subspan<14, 2>()) == state.relayPort;
    std::array<std::byte, 137> manager{};
    passed &= dawn::middleware::bap::activity_host_manager::response::encode_response(host, manager, written)
        && written == manager.size() && manager[0] == std::byte{2}
        && encoding::read_u64_be(std::span(manager).subspan<1, 8>()) == host;
    namespace group = dawn::middleware::gameplay::group;
    namespace bits = encoding::bits;
    group::ParameterUpdate update{};
    update.sessionId = host;
    update.carriedMask = 1ULL << 3;
    update.activityHost.hostId = host;
    update.activityHost.memberMask = 0xFFFFFFFFU;
    dawn::server::gameplay::group::publish_activity_host_endpoint(
        update.activityHost, dawn::core::settings::get());
    std::array<std::byte, 64> groupWire{};
    bits::Writer writer(groupWire);
    passed &= group::write_parameter_update(writer, update) && writer.finish(written);
    bits::Reader reader(std::span(groupWire).first(written), writer.bit_count());
    std::uint64_t scalar{}, session{}, selection{};
    passed &= reader.read(1, scalar) && scalar == 0 && bits::read_raw_u64(reader, session)
        && session == host && reader.read(2, scalar) && scalar == 0
        && reader.read(2, scalar) && scalar == 1;
    for (unsigned parameter = 0; parameter < 4; ++parameter)
        passed &= reader.read(1, scalar) && scalar == (parameter == 3 ? 1U : 0U);
    passed &= bits::read_raw_u64(reader, selection) && selection == 0
        && reader.read(64, scalar) && scalar == host && reader.skip(32)
        && reader.read(32, scalar);
    address.s_addr = htonl(static_cast<ULONG>(scalar));
    passed &= InetNtopA(AF_INET, &address, text.data(), text.size()) != nullptr
        && std::strcmp(text.data(), expectedAddress) == 0
        && reader.read(16, scalar) && scalar == state.relayPort;
    for (unsigned parameter = 4; parameter < 16; ++parameter)
        passed &= reader.read(1, scalar) && scalar == 0;
    passed &= reader.remaining_bits() == 0;
    return passed;
}

bool bap_endpoint() {
    using namespace dawn;
    auto& settings = core::settings::g_producerSettings;
    auto& external = settings.client.externalServer;
    settings.server.bapPort = 30974;
    state::SignOnState bootstrap{};
    bootstrap.bootstrapTokenPresent = true;
    state::session::Credentials issued{};
    external.enabled = false;
    bool passed = expect(state::session::issue(1, 2, bootstrap, 1000, issued)
                         && issued.signOn.relayAddress == 0x0100007FU
                         && issued.signOn.relayPort == 30974 && wire_endpoint(issued.signOn, "127.0.0.1"),
                         "disabled external mode publishes unchanged local BAP wire endpoint");
    external.enabled = true;
    external.address = {203, 0, 113, 19};
    passed &= expect(state::session::issue(1, 2, bootstrap, 1000, issued)
                         && issued.signOn.relayAddress == 0x137100CBU
                         && issued.signOn.relayPort == 30974 && wire_endpoint(issued.signOn, "203.0.113.19"),
                         "public BAP host reaches the native SignOn relay and external fields");
    external.portRoutes[0] = {30974, {198, 51, 100, 7}, 45712};
    external.portRouteCount = 1;
    passed &= expect(state::session::issue(1, 2, bootstrap, 1000, issued)
                         && issued.signOn.relayAddress == 0x076433C6U
                         && issued.signOn.relayPort == 45712 && wire_endpoint(issued.signOn, "198.51.100.7"),
                         "native SignOn publishes exact BAP tunnel host and port");
    state::session::Credentials authenticated{};
    passed &= expect(state::session::authenticate(issued.signOn.sessionToken, 1001, authenticated)
                         && authenticated.signOn.relayAddress == issued.signOn.relayAddress
                         && authenticated.signOn.relayPort == issued.signOn.relayPort,
                         "issued session retains advertised endpoint during authentication");
    external.enabled = false;
    settings.server.hasBapAdvertisedAddress = true;
    settings.server.bapAdvertisedAddress = {192, 0, 2, 31};
    settings.server.bapAdvertisedPort = 40074;
    passed &= expect(state::session::issue(1, 2, bootstrap, 1000, issued)
                         && issued.signOn.relayAddress == 0x1F0200C0U
                         && issued.signOn.relayPort == 40074 && wire_endpoint(issued.signOn, "192.0.2.31"),
                         "dedicated server publishes authored endpoint without client mode");
    external.enabled = true;
    passed &= expect(state::session::issue(1, 2, bootstrap, 1000, issued)
                         && issued.signOn.relayAddress == 0x1F0200C0U
                         && issued.signOn.relayPort == 40074,
                         "server publication wins over unrelated client port routes");
    settings.server.bapAdvertisedPort = 0;
    state::session::publish_native_bap_endpoint(bootstrap, settings);
    passed &= expect(bootstrap.relayAddress == 0x1F0200C0U && bootstrap.relayPort == 30974,
                         "omitted advertised port uses actual BAP listener port");
    settings.server.bapPort = 0;
    state::session::publish_native_bap_endpoint(bootstrap, settings);
    passed &= expect(bootstrap.relayAddress == 0x0100007FU && bootstrap.relayPort == 0,
                         "disabled relay sentinel remains local and port zero");
    state::session::shutdown();
    return passed;
}

bool startup_contract() {
    dawn::core::settings::Settings settings{};
    namespace routes = dawn::client::network::native_routes;
    bool passed = expect(!routes::valid_contract(settings),
        "legacy implicit relay input cannot disable socket guards");
    settings.server.hasBapAdvertisedAddress = true;
    passed &= expect(routes::valid_contract(settings), "explicit local server contract is valid");
    settings.server.gameplay.transportAddress = {203, 0, 113, 19};
    passed &= expect(!routes::valid_contract(settings), "gameplay requiring socket repair is refused");
    settings.server.gameplay.advertisedAddress = settings.server.gameplay.transportAddress;
    passed &= expect(routes::valid_contract(settings), "direct gameplay destination is valid");
    settings.client.externalServer.portRouteCount = 1;
    settings.client.externalServer.portRoutes[0] = {3075, {127, 0, 0, 1}, 40075};
    passed &= expect(!routes::valid_contract(settings), "discovery requiring port repair is refused");
    settings.client.externalServer.portRouteCount = 9;
    passed &= expect(!routes::valid_contract(settings), "invalid route count is refused before access");
    return passed;
}

bool discovery_data(const char* capturedImage, const char* capturedText) {
    namespace routes = dawn::client::hooks::egress::native_routes;
    std::vector<char> image;
    if (capturedImage != nullptr) {
        std::ifstream input(capturedImage, std::ios::binary);
        if (!input) return expect(false, "captured native rdata opens read-only");
        image.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    } else {
        image.push_back('\0');
        for (const auto host : routes::kDiscoveryHosts) {
            image.insert(image.end(), host.begin(), host.end());
            image.push_back('\0');
        }
    }
    routes::Plan plan{};
    constexpr std::string_view host = "203.0.113.19";
    bool passed = expect(routes::prepare(image, host, plan),
                         "all20 exact initialized STUN literals validate before mutation");
    const auto original = image;
    auto broken = image;
    if (passed) broken[plan.offsets.back()] = '!';
    const auto brokenBefore = broken;
    routes::Plan missing{};
    passed &= expect(!routes::prepare(broken, host, missing) && broken != original,
                         "missing literal rejects the entire candidate");
    passed &= expect(!routes::apply(broken, host, plan) && broken == brokenBefore,
                         "failed apply makes no partial hostname substitutions");
    passed &= expect(!routes::prepare(image, "backend.example", missing) && image == original,
                         "hostname input cannot introduce DNS dependence");
    auto duplicate = image;
    duplicate.push_back('\0');
    duplicate.insert(duplicate.end(), routes::kDiscoveryHosts.front().begin(),
                     routes::kDiscoveryHosts.front().end());
    duplicate.push_back('\0');
    passed &= expect(!routes::prepare(duplicate, host, missing),
                         "ambiguous literal aborts the candidate");
    passed &= expect(routes::apply(image, host, plan),
                         "validated candidate changes initialized data without interception");
    for (std::size_t index = 0; index < plan.offsets.size(); ++index) {
        passed &= expect(std::strcmp(image.data() + plan.offsets[index], host.data()) == 0,
                         "each original STUN pointer now names the configured numeric host");
    }
    if (capturedText != nullptr) passed &= replay_lookup(original, image, plan, capturedText);
    ADDRINFOA hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;
    PADDRINFOA address{};
    passed &= expect(::getaddrinfo(image.data() + plan.offsets.front(), "3074", &hints,
                                  &address) == 0 && address != nullptr,
                         "unhooked native resolver accepts rewritten STUN initialized data");
    if (address != nullptr) {
        const auto endpoint = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
        passed &= expect(ntohl(endpoint->sin_addr.s_addr) == 0xCB007113U
                             && ntohs(endpoint->sin_port) == 3074,
                         "native lookup preserves exact public STUN address and port");
        ::freeaddrinfo(address);
    }
    return passed;
}

bool loaded_discovery_image(const char* path) {
    if (path == nullptr) return expect(false, "test image path supplied");
    const auto image = LoadLibraryA(path);
    if (image == nullptr) return expect(false, "isolated test DLL loads");
    const auto names = reinterpret_cast<const char*>(GetProcAddress(image, "nativeDiscoveryNames"));
    dawn::core::settings::client::external::Settings external{};
    external.address = {203, 0, 113, 19};
    std::memcpy(external.host.data(), "203.0.113.19", sizeof("203.0.113.19"));
    external.portRoutes[0] = {3074, external.address, 40000};
    external.portRouteCount = 1;
    namespace routes = dawn::client::network::native_routes;
    bool passed = expect(names != nullptr
        && !routes::publish_discovery(image, external)
        && std::strcmp(names + 1, "stun.us.demonware.net") == 0,
        "unsupported public discovery port leaves loaded data intact");
    external.portRoutes[0].targetPort = 3074;
    passed &= expect(routes::publish_discovery(image, external),
        "production startup producer changes only validated loaded image data");
    std::size_t offset = 1;
    for (const auto original : dawn::client::hooks::egress::native_routes::kDiscoveryHosts) {
        MEMORY_BASIC_INFORMATION page{};
        passed &= expect(std::strcmp(names + offset, external.host.data()) == 0
            && VirtualQuery(names + offset, &page, sizeof page) == sizeof page
            && page.Protect == PAGE_READONLY,
            "every native destination changes and its original read-only permission returns");
        offset += original.size() + 1;
    }
    FreeLibrary(image);
    return passed;
}
}

int main(int argc, char** argv) {
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup) != 0) return 1;
    const bool passed = bap_endpoint() && startup_contract()
        && discovery_data(argc > 2 ? argv[1] : nullptr,
                          argc > 3 ? argv[2] : nullptr)
        && loaded_discovery_image(argc > 1 ? argv[argc - 1] : nullptr);
    WSACleanup();
    if (passed) std::puts("PASS: native SignOn BAP and initialized STUN data publish public routes");
    return passed ? 0 : 1;
}
