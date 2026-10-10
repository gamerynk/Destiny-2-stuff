#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "client/hooks/egress/internal.h"
#include "../dawn/vendor/detours/detours.h"
#include "client/hooks/egress/lifecycle/internal.h"
#include "client/hooks/egress/lifecycle/ansi_resolver_layer.h"
#include "client/hooks/egress/platform/abi.h"
#include "client/hooks/egress/policy/policy.h"
#include "core/logging/log.h"
#include "core/settings/settings.h"

namespace dawn::core::settings {
Settings g_surfaceSettings{};
const Settings& get() noexcept { return g_surfaceSettings; }
}

namespace dawn::core::log {
void write(Channel, Level, std::string_view) noexcept {}
}

namespace dawn::client::hooks::egress {
SRWLOCK g_lock{SRWLOCK_INIT};
std::array<hooking::detour::Handle, kHookCount> g_handles{};
}

namespace dawn::client::hooks::egress::policy {
unsigned g_wideNameDecisions{};
unsigned g_receiveReports{}, g_controlDecisions{};
bool allow_native_sdr_passthrough(const void*) noexcept { return true; }
void report_native_sdr_io(NativeSdrPath path, bool, std::size_t, int, int) noexcept {
    if (path == NativeSdrPath::recvFrom || path == NativeSdrPath::wsaRecvFrom) ++g_receiveReports;
}
void log_decision(SocketOperation, bool, bool) noexcept {}
void log_name_decision(NameOperation operation, bool, std::string_view, unsigned, unsigned,
                       const void*) noexcept { if (operation == NameOperation::control) ++g_controlDecisions; }
void log_name_decision(NameOperation, bool, std::wstring_view, unsigned, unsigned,
                       const void*) noexcept { ++g_wideNameDecisions; }
}

namespace {
bool expect(bool value, const char* label) {
    if (!value) std::fprintf(stderr, "FAIL: %s (error %d)\n", label, WSAGetLastError());
    return value;
}

bool surface() {
    using namespace dawn::client::hooks::egress;
    bool passed = expect(lifecycle::load_modules(), "system modules load");
    std::array<dawn::client::hooking::detour::Spec, kHookCount> specs{};
    std::array<const char*, kHookCount> names{};
    std::array<bool, kHookCount> resolved{};
    std::size_t count{};
    passed &= expect(lifecycle::resolve_specs(specs, names, resolved, count),
                     "every required routing/diagnostic export resolves");
    const HMODULE winsock = lifecycle::module_handle(lifecycle::ModuleSlot::winsock);
    const bool layered = lifecycle::native_ansi_uses_wide_guard(winsock);
    passed &= expect(count == (layered ? lifecycle::kRequiredHookCount : kHookCount),
                     "known ANSI wrapper skips optional slot; unknown wrapper keeps compatibility guard");
    std::array<unsigned char, lifecycle::kVerifiedAnsiResolverCode.size()> copy =
        lifecycle::kVerifiedAnsiResolverCode;
    std::int32_t displacement{};
    std::memcpy(&displacement, copy.data() + 0xB9, sizeof(displacement));
    const auto targetAddress = reinterpret_cast<std::uintptr_t>(copy.data()) + 0xBD
        + static_cast<std::intptr_t>(displacement);
    const void* const target = reinterpret_cast<const void*>(targetAddress);
    passed &= expect(lifecycle::verified_ansi_resolver_bytes(copy, target),
                     "complete known wrapper and its actual wide target validate");
    copy.back() ^= 1;
    passed &= expect(!lifecycle::verified_ansi_resolver_bytes(copy, target),
                     "any changed branch byte keeps the ANSI compatibility hook");
    passed &= expect(!lifecycle::verified_ansi_resolver_bytes(lifecycle::kVerifiedAnsiResolverCode, nullptr),
                     "unknown forwarding target keeps the ANSI compatibility hook");
    constexpr std::array retired{"recv", "WSARecv",
        "WSAConnect", "WSAConnectByNameA", "WSAConnectByNameW", "WSAConnectByList",
        "WSASendDisconnect", "WSAJoinLeaf", "GetAddrInfoExA", "DnsQuery_A", "DnsQuery_W", "DnsQuery_UTF8", "DnsQueryRaw", "getaddrinfo"};
    for (std::size_t index = 0; index < count; ++index) {
        for (const char* name : retired) {
            if (!layered && std::string_view(name) == "getaddrinfo") continue;
            const HMODULE owner = std::string_view(name).starts_with("DnsQuery")
                ? lifecycle::module_handle(lifecycle::ModuleSlot::dns) : winsock;
            passed &= expect(specs[index].target != reinterpret_cast<void*>(GetProcAddress(owner, name)),
                             "receive/control exports are outside the detour transaction");
        }
        g_handles[index].original = specs[index].target;
    }
    return passed;
}

bool native_ansi_resolver_layer() {
    using namespace dawn::client::hooks::egress;
    if (!lifecycle::native_ansi_uses_wide_guard(lifecycle::module_handle(lifecycle::ModuleSlot::winsock))) {
        std::puts("Native ANSI replay skipped for unknown wrapper; compatibility hook remains required");
        return true;
    }
    void** const original = &g_handles[static_cast<std::size_t>(HookSlot::getAddrInfoW)].original;
    const auto replacement = reinterpret_cast<void*>(&resolver::address_info_w);
    if (DetourTransactionBegin() != NO_ERROR) return false;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR
        || DetourAttach(original, replacement) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    if (DetourTransactionCommit() != NO_ERROR) return false;
    auto& external = dawn::core::settings::g_surfaceSettings.client.externalServer;
    external.enabled = true;
    external.address = {203, 0, 113, 27};
    constexpr char publicHost[] = "203.0.113.27";
    constexpr wchar_t publicHostWide[] = L"203.0.113.27";
    std::memcpy(external.host.data(), publicHost, sizeof(publicHost));
    std::memcpy(external.hostWide.data(), publicHostWide, sizeof(publicHostWide));
    bool passed = true;
    for (const int family : {AF_UNSPEC, AF_INET, AF_INET6}) {
        ADDRINFOA hints{};
        hints.ai_family = family;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        hints.ai_flags = AI_CANONNAME;
        PADDRINFOA result{};
        const unsigned decisions = policy::g_wideNameDecisions;
        const int status = ::getaddrinfo("never-resolve.invalid", "3074", &hints, &result);
        passed &= expect(status == 0 && result != nullptr
            && policy::g_wideNameDecisions == decisions + 1,
            "native ANSI resolver reaches only retained wide guard");
        if (result != nullptr) {
            if (family == AF_INET6) {
                const auto* address = reinterpret_cast<const sockaddr_in6*>(result->ai_addr);
                passed &= expect(result->ai_family == AF_INET6
                    && IN6_IS_ADDR_LOOPBACK(&address->sin6_addr)
                    && ntohs(address->sin6_port) == 3074, "ANSI IPv6 result retains port and family");
            } else {
                const auto* address = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
                passed &= expect(result->ai_family == AF_INET
                    && ntohl(address->sin_addr.s_addr) == 0xCB00711B
                    && ntohs(address->sin_port) == 3074, "ANSI result is the guarded numeric endpoint");
            }
            passed &= expect(result->ai_socktype == SOCK_DGRAM && result->ai_protocol == IPPROTO_UDP,
                             "ANSI wrapper preserves socket constraints and output conversion");
            ::freeaddrinfo(result);
        }
    }
    for (const char* name : {static_cast<const char*>(nullptr), ""}) {
        PADDRINFOA result{};
        const unsigned decisions = policy::g_wideNameDecisions;
        passed &= expect(::getaddrinfo(name, "3074", nullptr, &result) == EAI_FAIL
            && result == nullptr && policy::g_wideNameDecisions == decisions + 1,
            "ANSI malformed name with valid output is refused by retained wide guard");
    }
    PADDRINFOA result{};
    passed &= expect(::getaddrinfo("never-resolve.invalid", "invalid-service", nullptr, &result)
        != 0 && result == nullptr, "native ANSI service validation cannot bypass wide routing");
    if (DetourTransactionBegin() != NO_ERROR) return false;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR
        || DetourDetach(original, replacement) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    const bool detached = DetourTransactionCommit() == NO_ERROR;
    external = {};
    return detached && passed;
}

bool native_stream_receives() {
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const SOCKET client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int length = sizeof(endpoint);
    if (listener == INVALID_SOCKET || client == INVALID_SOCKET
        || ::bind(listener, reinterpret_cast<sockaddr*>(&endpoint), length) == SOCKET_ERROR
        || ::listen(listener, 1) == SOCKET_ERROR
        || ::getsockname(listener, reinterpret_cast<sockaddr*>(&endpoint), &length) == SOCKET_ERROR
        || ::connect(client, reinterpret_cast<sockaddr*>(&endpoint), length) == SOCKET_ERROR) {
        return expect(false, "native local stream pair opens");
    }
    const SOCKET peer = ::accept(listener, nullptr, nullptr);
    constexpr char payload[] = "native receive";
    std::array<char, sizeof(payload)> received{};
    bool passed = expect(peer != INVALID_SOCKET, "native stream is accepted");
    passed &= expect(::send(peer, payload, sizeof(payload), 0) == sizeof(payload)
                         && ::recv(client, received.data(), static_cast<int>(received.size()),
                                    MSG_WAITALL) == sizeof(payload)
                         && std::memcmp(payload, received.data(), sizeof(payload)) == 0,
                     "unhooked recv receives the complete provider payload");
    received.fill(0);
    WSABUF buffer{static_cast<ULONG>(received.size()), received.data()};
    DWORD bytes{};
    DWORD flags{};
    WSAOVERLAPPED operation{};
    operation.hEvent = WSACreateEvent();
    const int submitted = ::WSARecv(client, &buffer, 1, &bytes, &flags, &operation, nullptr);
    passed &= expect(submitted == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING,
                     "unhooked WSARecv preserves pending operation semantics");
    passed &= expect(::send(peer, payload, sizeof(payload), 0) == sizeof(payload)
                         && WSAWaitForMultipleEvents(1, &operation.hEvent, TRUE, 3000, FALSE)
                                == WSA_WAIT_EVENT_0
                         && WSAGetOverlappedResult(client, &operation, &bytes, FALSE, &flags)
                         && bytes == sizeof(payload)
                         && std::memcmp(payload, received.data(), sizeof(payload)) == 0,
                     "unhooked WSARecv completes into caller storage");
    ::shutdown(peer, SD_SEND);
    passed &= expect(::recv(client, received.data(), 1, 0) == 0,
                     "unhooked recv preserves orderly shutdown");
    WSACloseEvent(operation.hEvent);
    closesocket(peer);
    closesocket(client);
    closesocket(listener);
    return passed;
}

bool native_datagram_receives() {
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const SOCKET sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int length = sizeof(endpoint);
    bool passed = expect(::bind(socket, reinterpret_cast<sockaddr*>(&endpoint), length) == 0
                         && ::getsockname(socket, reinterpret_cast<sockaddr*>(&endpoint), &length) == 0,
                         "native UDP listener binds an ephemeral loopback endpoint");
    DWORD available{};
    DWORD bytes{};
    passed &= expect(dawn::client::hooks::egress::winsock::control::socket_control(socket, FIONREAD, nullptr, 0, &available, sizeof(available),
                               &bytes, nullptr, nullptr) == 0,
                         "observed WSAIoctl keeps provider control functionality");
    GUID extension = WSAID_WSARECVMSG;
    LPFN_WSARECVMSG messageReceive{};
    passed &= expect(dawn::client::hooks::egress::winsock::control::socket_control(socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                               &extension, sizeof(extension), &messageReceive, sizeof(messageReceive),
                               &bytes, nullptr, nullptr) == 0 && messageReceive != nullptr,
                         "observed WSAIoctl keeps native extension pointer access");
    constexpr char payload[] = "native datagram";
    std::array<char, sizeof(payload)> received{};
    sockaddr_in source{};
    int sourceLength = sizeof(source);
    passed &= expect(::sendto(sender, payload, sizeof(payload), 0,
                                reinterpret_cast<sockaddr*>(&endpoint), length) == sizeof(payload)
                         && dawn::client::hooks::egress::winsock::reception::receive_bytes_from(socket, received.data(), static_cast<int>(received.size()), 0,
                                       reinterpret_cast<sockaddr*>(&source), &sourceLength) == sizeof(payload)
                         && std::memcmp(payload, received.data(), sizeof(payload)) == 0,
                         "observed recvfrom preserves payload and source address");
    WSABUF buffer{static_cast<ULONG>(received.size()), received.data()};
    DWORD flags{};
    WSAOVERLAPPED operation{};
    operation.hEvent = WSACreateEvent();
    received.fill(0);
    const int submitted = dawn::client::hooks::egress::winsock::reception::receive_buffers_from(socket, &buffer, 1, &bytes, &flags,
                                        reinterpret_cast<sockaddr*>(&source), &sourceLength,
                                        &operation, nullptr);
    passed &= expect(submitted == SOCKET_ERROR && WSAGetLastError() == WSA_IO_PENDING,
                         "observed WSARecvFrom preserves pending receive semantics");
    passed &= expect(::sendto(sender, payload, sizeof(payload), 0,
                                reinterpret_cast<sockaddr*>(&endpoint), length) == sizeof(payload)
                         && WSAWaitForMultipleEvents(1, &operation.hEvent, TRUE, 3000, FALSE)
                                == WSA_WAIT_EVENT_0
                         && WSAGetOverlappedResult(socket, &operation, &bytes, FALSE, &flags)
                         && bytes == sizeof(payload)
                         && std::memcmp(payload, received.data(), sizeof(payload)) == 0,
                         "observed WSARecvFrom completes into caller buffers");
    WSACloseEvent(operation.hEvent);
    closesocket(socket);
    closesocket(sender);
    passed &= expect(dawn::client::hooks::egress::policy::g_receiveReports >= 2
        && dawn::client::hooks::egress::policy::g_controlDecisions >= 2,
        "datagram reception and provider control diagnostic paths are preserved");
    return passed;
}

bool retained_policy_boundaries() {
    using namespace dawn::client::hooks::egress;
    g_handles = {};
    bool passed = expect(winsock::connection::connect_socket(INVALID_SOCKET, nullptr, 0)
                             == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
                         "connect remains fail-closed on an unavailable routing boundary");
    passed &= expect(winsock::transmission::send_bytes(INVALID_SOCKET, nullptr, 0, 0)
                         == SOCKET_ERROR && WSAGetLastError() == WSAEACCES,
                         "send cannot bypass the configured peer boundary");
    DWORD bytes = 999;
    passed &= expect(winsock::transmission::send_buffers(INVALID_SOCKET, nullptr, 0, &bytes,
                         0, nullptr, nullptr) == SOCKET_ERROR && bytes == 0,
                         "WSASend clears submitted output on policy refusal");
    passed &= expect(winsock::transmission::send_bytes_to(INVALID_SOCKET, nullptr, 0, 0,
                         nullptr, 0) == SOCKET_ERROR,
                         "sendto cannot bypass explicit endpoint validation");
    bytes = 999;
    passed &= expect(winsock::transmission::send_buffers_to(INVALID_SOCKET, nullptr, 0, &bytes,
                         0, nullptr, 0, nullptr, nullptr) == SOCKET_ERROR && bytes == 0,
                         "WSASendTo keeps refusal output semantics");
    bytes = 999;
    passed &= expect(winsock::transmission::send_message(INVALID_SOCKET, nullptr, 0, &bytes,
                         nullptr, nullptr) == SOCKET_ERROR && bytes == 0,
                         "WSASendMsg refuses absent messages without dereferencing them");
    passed &= expect(extensions::transmit_file(INVALID_SOCKET, nullptr, 0, 0, nullptr,
                         nullptr, 0) == FALSE && WSAGetLastError() == WSAEACCES,
                         "TransmitFile preserves configured peer policy");
    PADDRINFOW addressW{};
    PADDRINFOEXW extended{};
    HANDLE request{};
    passed &= expect(resolver::address_info_w(L"host", L"3074", nullptr, &addressW)
                         == WSANO_RECOVERY && addressW == nullptr,
                         "GetAddrInfoW cannot reach an unpublished original");
    passed &= expect(resolver::address_info_ex_w(L"host", L"3074", NS_DNS, nullptr, nullptr,
                         &extended, nullptr, nullptr, nullptr, &request) == WSANO_RECOVERY
                         && extended == nullptr && request == nullptr,
                         "GetAddrInfoExW does not leave an asynchronous request on refusal");
    passed &= expect(resolver::host_by_name(nullptr) == nullptr
                         && WSAGetLastError() == WSAHOST_NOT_FOUND,
                         "gethostbyname retains its null-input contract");
    passed &= expect(resolver::host_by_address(nullptr, 0, AF_INET) == nullptr
                         && WSAGetLastError() == WSAHOST_NOT_FOUND,
                         "gethostbyaddr retains reverse policy refusal");
    char node[]{'x', 0};
    wchar_t wide[]{L'x', 0};
    passed &= expect(resolver::name_info_a(nullptr, 0, node, 2, nullptr, 0, 0)
                         == WSANO_RECOVERY && node[0] == '\0',
                         "getnameinfo clears stale caller output on refusal");
    passed &= expect(resolver::name_info_w(nullptr, 0, wide, 2, nullptr, 0, 0)
                         == WSANO_RECOVERY && wide[0] == L'\0',
                         "GetNameInfoW clears stale caller output on refusal");

    DNS_QUERY_RESULT result{};
    DNS_QUERY_CANCEL cancellation{};
    passed &= expect(dns::query_ex(nullptr, &result, &cancellation) == DNS_ERROR_RCODE_REFUSED
                         && result.QueryStatus == DNS_ERROR_RCODE_REFUSED,
                         "DnsQueryEx preserves caller-owned refusal status");
    return passed;
}
}

int main() {
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup) != 0) return 1;
    const bool passed = surface() && native_ansi_resolver_layer()
                         && native_stream_receives() && native_datagram_receives()
                         && retained_policy_boundaries();
    dawn::client::hooks::egress::lifecycle::release_modules();
    WSACleanup();
    if (passed) std::puts("PASS: reduced egress surface preserves native receives and provider controls");
    return passed ? 0 : 1;
}
