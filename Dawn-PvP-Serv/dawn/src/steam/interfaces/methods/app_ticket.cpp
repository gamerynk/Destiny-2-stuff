#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>

#include "../../../core/identity/device_identity.h"
#include "../../../core/identity/local_ticket.h"
#include "../../../core/logging/log.h"
#include "../internal.h"

namespace dawn::steam::interfaces::methods {
namespace {

constexpr int kEncryptedAppTicketCallback = 154;
constexpr int kResultOk = 1;
constexpr DWORD kTicketSize = static_cast<DWORD>(
    kUniversalClientBuild ? core::identity::device_identity::kProofSize
                          : core::identity::local_ticket::kTicketSize);

struct EncryptedAppTicketResponse final {
    int result{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<unsigned char, kTicketSize> g_ticket{};
bool g_generated{};

[[nodiscard]] bool ensure_ticket(bool renew = false) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (renew || !g_generated) {
        // SignOn renews during long sessions; reusing the first proof fails renewal after its
        // 15-minute window and the BAP session expires. Clear the old proof so failure shows
        g_generated = false;
        SecureZeroMemory(g_ticket.data(), g_ticket.size());
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        HMODULE process = GetModuleHandleW(nullptr);
        if constexpr (kUniversalClientBuild) {
            g_generated = process != nullptr && now > 0
                          && core::identity::device_identity::initialize(process)
                          && core::identity::device_identity::issue(
                              core::identity::device_identity::Scope::signOn,
                              static_cast<std::uint64_t>(now),
                              std::span<std::byte, core::identity::device_identity::kProofSize>(
                                  reinterpret_cast<std::byte*>(g_ticket.data()),
                                  g_ticket.size()));
        } else {
            g_generated = process != nullptr && now > 0
                          && core::identity::local_ticket::initialize(process)
                          && core::identity::local_ticket::issue(
                              local_steam_id(),
                              static_cast<std::uint64_t>(now),
                              std::span<std::byte, core::identity::local_ticket::kTicketSize>(
                                  reinterpret_cast<std::byte*>(g_ticket.data()),
                                  g_ticket.size()));
        }
    }
    const bool ready = g_generated;
    ReleaseSRWLockExclusive(&g_lock);
    return ready;
}

} // namespace

ApiCall request_encrypted_app_ticket([[maybe_unused]] void* self,
                                     [[maybe_unused]] const void* data,
                                     [[maybe_unused]] int dataSize) noexcept {
    if (!ensure_ticket(true)) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=steam stage=app_ticket_request result=fail reason=material");
        return 0;
    }
    const ApiCall call = next_api_call();
    const EncryptedAppTicketResponse response{kResultOk};
    if (!queue_callback(kEncryptedAppTicketCallback, call, &response, sizeof(response))) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=steam stage=app_ticket_request result=fail reason=queue");
        return 0;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::debug,
                     "ev=steam stage=app_ticket_request result=ok");
    return call;
}

bool get_encrypted_app_ticket([[maybe_unused]] void* self,
                              void* ticket,
                              int maxTicket,
                              DWORD* ticketSize) noexcept {
    if (ticketSize != nullptr) {
        *ticketSize = 0;
    }
    if (ticket == nullptr || maxTicket < static_cast<int>(kTicketSize) || ticketSize == nullptr
        || !ensure_ticket()) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::warn,
                         "ev=steam stage=app_ticket_read result=fail reason=capacity");
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    std::memcpy(ticket, g_ticket.data(), kTicketSize);
    ReleaseSRWLockShared(&g_lock);
    *ticketSize = kTicketSize;
    core::log::write(core::log::Channel::client,
                     core::log::Level::debug,
                     "ev=steam stage=app_ticket_read result=ok");
    return true;
}

} // namespace dawn::steam::interfaces::methods
