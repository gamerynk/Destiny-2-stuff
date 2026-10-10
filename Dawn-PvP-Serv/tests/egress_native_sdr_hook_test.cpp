#include <array>
#include <cstdio>
#include <cstring>

#include "client/hooks/egress/internal.h"
#include "client/hooks/egress/policy/policy.h"
#include "client/hooks/egress/winsock/discovery/egress_discovery_responder.h"
#include "client/hooks/egress/winsock/transmission/replacements.h"
#include "core/logging/log.h"
#include "core/settings/settings.h"

namespace dawn::core::settings {
Settings g_testSettings{};
const Settings& get() noexcept { return g_testSettings; }
}

namespace dawn::core::log {
void write(Channel, Level, std::string_view) noexcept {}
}

namespace dawn::client::hooks::egress {
SRWLOCK g_lock{SRWLOCK_INIT};
std::array<hooking::detour::Handle, kHookCount> g_handles{};
}

namespace dawn::client::hooks::egress::winsock::discovery {
Result handle(SOCKET, std::span<const std::byte>, int, const sockaddr*, int,
              SendTo) noexcept { return {}; }
}

namespace dawn::client::hooks::egress::policy {
bool g_nativeCaller{};
unsigned g_reported{};
bool allow_native_sdr_passthrough(const void*) noexcept { return g_nativeCaller; }
void report_native_sdr_io(NativeSdrPath, bool, std::size_t, int, int) noexcept {
    ++g_reported;
    WSASetLastError(7777);
}
void log_decision(SocketOperation, bool, bool) noexcept {}
}

namespace {
sockaddr_in g_seen{};
bool g_seenNull{};
unsigned g_calls{};
int g_fakeWsaReturn{};
int g_fakeWsaError{};
DWORD g_seenFlags{};
LPWSAOVERLAPPED g_seenOverlapped{};
LPWSAOVERLAPPED_COMPLETION_ROUTINE g_seenCompletion{};

void CALLBACK fake_completion(DWORD, DWORD, LPWSAOVERLAPPED, DWORD) {}

int WSAAPI fake_send(SOCKET, const char*, int length, int) {
    ++g_calls;
    return length;
}

int WSAAPI fake_wsa_send(SOCKET, LPWSABUF buffers, DWORD, LPDWORD bytesSent, DWORD,
                         LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE) {
    ++g_calls;
    if (bytesSent != nullptr) *bytesSent = buffers[0].len;
    return 0;
}

int WSAAPI fake_sendto(SOCKET, const char*, int length, int, const sockaddr* target,
                       int targetLength) {
    ++g_calls;
    g_seenNull = target == nullptr;
    g_seen = {};
    if (target != nullptr && targetLength >= static_cast<int>(sizeof(g_seen))) {
        std::memcpy(&g_seen, target, sizeof(g_seen));
    }
    return length;
}

int WSAAPI fake_wsa_sendto(SOCKET, LPWSABUF buffers, DWORD, LPDWORD bytesSent, DWORD flags,
                            const sockaddr* target, int targetLength, LPWSAOVERLAPPED overlapped,
                            LPWSAOVERLAPPED_COMPLETION_ROUTINE completion) {
    ++g_calls;
    g_seenFlags = flags;
    g_seenOverlapped = overlapped;
    g_seenCompletion = completion;
    g_seenNull = target == nullptr;
    g_seen = {};
    if (target != nullptr && targetLength >= static_cast<int>(sizeof(g_seen))) {
        std::memcpy(&g_seen, target, sizeof(g_seen));
    }
    if (bytesSent != nullptr) *bytesSent = buffers[0].len;
    WSASetLastError(g_fakeWsaError);
    return g_fakeWsaReturn;
}

INT PASCAL fake_wsa_sendmsg(SOCKET, LPWSAMSG message, DWORD, LPDWORD bytesSent,
                             LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE) {
    ++g_calls;
    g_seenNull = message->name == nullptr;
    g_seen = {};
    if (message->name != nullptr && message->namelen >= static_cast<int>(sizeof(g_seen))) {
        std::memcpy(&g_seen, message->name, sizeof(g_seen));
    }
    if (bytesSent != nullptr) *bytesSent = message->lpBuffers[0].len;
    return 0;
}

bool expect(bool result, const char* label) {
    if (!result) std::fprintf(stderr, "FAIL: %s\n", label);
    return result;
}

sockaddr_in endpoint(unsigned char first) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(4567);
    const std::array<unsigned char, 4> octets{first, 51, 100, 3};
    std::memcpy(&address.sin_addr, octets.data(), octets.size());
    return address;
}

bool same_address(const sockaddr_in& left, const sockaddr_in& right) {
    return left.sin_family == right.sin_family && left.sin_port == right.sin_port
           && left.sin_addr.s_addr == right.sin_addr.s_addr;
}
}

int main() {
    using namespace dawn::client::hooks::egress;
    using namespace dawn::client::hooks::egress::winsock::transmission;
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup) != 0) return 1;
    const SOCKET udp = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const SOCKET tcp = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (udp == INVALID_SOCKET || tcp == INVALID_SOCKET) return 1;
    auto& external = dawn::core::settings::g_testSettings.client.externalServer;
    external.enabled = true;
    external.address = {203, 0, 113, 5};
    g_handles[static_cast<std::size_t>(HookSlot::sendTo)].original =
        reinterpret_cast<void*>(&fake_sendto);
    g_handles[static_cast<std::size_t>(HookSlot::send)].original =
        reinterpret_cast<void*>(&fake_send);
    g_handles[static_cast<std::size_t>(HookSlot::wsaSend)].original =
        reinterpret_cast<void*>(&fake_wsa_send);
    g_handles[static_cast<std::size_t>(HookSlot::wsaSendTo)].original =
        reinterpret_cast<void*>(&fake_wsa_sendto);
    g_handles[static_cast<std::size_t>(HookSlot::wsaSendMsg)].original =
        reinterpret_cast<void*>(&fake_wsa_sendmsg);

    const sockaddr_in remote = endpoint(198);
    sockaddr_in redirected = remote;
    const std::array<unsigned char, 4> redirectOctets{203, 0, 113, 5};
    std::memcpy(&redirected.sin_addr, redirectOctets.data(), redirectOctets.size());
    char payload[]{'x'};
    WSABUF buffer{1, payload};
    DWORD bytesSent{};
    bool passed = true;

    policy::g_nativeCaller = true;
    passed &= expect(send_bytes_to(udp, payload, 1, 0,
                                  reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 1
                         && same_address(g_seen, remote),
                     "native UDP sendto keeps explicit destination");
    passed &= expect(send_buffers_to(udp, &buffer, 1, &bytesSent, 0,
                                    reinterpret_cast<const sockaddr*>(&remote), sizeof(remote),
                                    nullptr, nullptr) == 0 && same_address(g_seen, remote),
                     "native UDP WSASendTo keeps explicit destination");
    WSAMSG message{};
    message.name = reinterpret_cast<sockaddr*>(const_cast<sockaddr_in*>(&remote));
    message.namelen = sizeof(remote);
    message.lpBuffers = &buffer;
    message.dwBufferCount = 1;
    passed &= expect(send_message(udp, &message, 0, &bytesSent, nullptr, nullptr) == 0
                         && same_address(g_seen, remote),
                     "native UDP WSASendMsg keeps explicit destination");

    policy::g_nativeCaller = false;
    passed &= expect(send_buffers_to(udp, &buffer, 1, &bytesSent, 0,
                                    reinterpret_cast<const sockaddr*>(&remote), sizeof(remote),
                                    nullptr, nullptr) == 0 && same_address(g_seen, redirected),
                     "non-native WSASendTo still redirects");
    policy::g_nativeCaller = true;
    passed &= expect(send_buffers_to(tcp, &buffer, 1, &bytesSent, 0,
                                    reinterpret_cast<const sockaddr*>(&remote), sizeof(remote),
                                    nullptr, nullptr) == 0 && same_address(g_seen, redirected),
                     "native TCP cannot bypass redirect");
    sockaddr_in wrongFamily = remote;
    wrongFamily.sin_family = AF_INET6;
    const unsigned callsBeforeDeny = g_calls;
    passed &= expect(send_buffers_to(udp, &buffer, 1, &bytesSent, 0,
                                    reinterpret_cast<const sockaddr*>(&wrongFamily),
                                    sizeof(wrongFamily), nullptr, nullptr) == SOCKET_ERROR
                         && WSAGetLastError() == WSAEACCES && g_calls == callsBeforeDeny,
                     "non-IPv4 native destination remains denied");

    const sockaddr_in connected = endpoint(198);
    if (::connect(udp, reinterpret_cast<const sockaddr*>(&connected), sizeof(connected)) != 0) {
        return 1;
    }
    passed &= expect(send_buffers_to(udp, &buffer, 1, &bytesSent, 0, nullptr, 0,
                                    nullptr, nullptr) == 0 && g_seenNull,
                     "native UDP WSASendTo preserves connected destination");
    message.name = nullptr;
    message.namelen = 0;
    passed &= expect(send_message(udp, &message, 0, &bytesSent, nullptr, nullptr) == 0
                         && g_seenNull,
                     "native UDP WSASendMsg preserves connected destination");
    passed &= expect(send_bytes(udp, payload, 1, 0) == 1,
                     "native UDP connected send calls original");
    passed &= expect(send_buffers(udp, &buffer, 1, &bytesSent, 0, nullptr, nullptr) == 0,
                     "native UDP connected WSASend calls original");

    WSAOVERLAPPED overlapped{};
    g_fakeWsaReturn = SOCKET_ERROR;
    g_fakeWsaError = WSA_IO_PENDING;
    passed &= expect(send_buffers_to(udp, &buffer, 1, &bytesSent, 0x20,
                                    reinterpret_cast<const sockaddr*>(&remote), sizeof(remote),
                                    &overlapped, &fake_completion) == SOCKET_ERROR
                         && WSAGetLastError() == WSA_IO_PENDING
                         && same_address(g_seen, remote) && g_seenFlags == 0x20
                         && g_seenOverlapped == &overlapped
                         && g_seenCompletion == &fake_completion,
                     "pending native WSASendTo preserves arguments and last error");
    g_fakeWsaError = WSAECONNRESET;
    passed &= expect(send_buffers_to(udp, &buffer, 1, &bytesSent, 0,
                                    reinterpret_cast<const sockaddr*>(&remote), sizeof(remote),
                                    nullptr, nullptr) == SOCKET_ERROR
                         && WSAGetLastError() == WSAECONNRESET,
                     "failed native WSASendTo preserves last error");
    passed &= expect(policy::g_reported == 9, "diagnostics count only native UDP paths");

    closesocket(tcp);
    closesocket(udp);
    WSACleanup();
    if (passed) std::puts("PASS: native SDR hooks preserve UDP destinations and redirect others");
    return passed ? 0 : 1;
}
