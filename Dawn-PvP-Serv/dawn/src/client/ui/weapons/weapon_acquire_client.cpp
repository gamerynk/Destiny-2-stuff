#include "weapon_acquire_client.h"
#include "weapon_acquire_request.h"
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include "../../../core/identity/device_identity.h"
#include "../../../core/identity/local_ticket.h"
#include "../../../steam/runtime/runtime.h"
#include "../../hooks/external_server/transport.h"

namespace dawn::client::ui::weapons::acquire_client {
namespace {
using detail::Selection;
SRWLOCK g_lock{SRWLOCK_INIT};
HANDLE g_worker{};
Selection g_selection{};
View g_view{};
void fail(const char* error) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_view = {};
    g_view.status = Status::failed;
    std::snprintf(g_view.error.data(), g_view.error.size(), "%s", error);
    ReleaseSRWLockExclusive(&g_lock);
}
DWORD WINAPI worker(void*) noexcept {
    Selection selected{};
    AcquireSRWLockShared(&g_lock);
    selected = g_selection;
    ReleaseSRWLockShared(&g_lock);
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::array<std::byte, 1024> body{};
    std::size_t proofSize{};
    bool issued{};
    if constexpr (steam::kUniversalClientBuild) {
        proofSize = core::identity::device_identity::kProofSize;
        issued = core::identity::device_identity::issue(core::identity::device_identity::Scope::weaponAcquire,
            now, std::span<std::byte, core::identity::device_identity::kProofSize>(body.data(), proofSize));
    } else {
        proofSize = core::identity::local_ticket::kTicketSize;
        issued = core::identity::local_ticket::issue(steam::local_steam_id(), now,
            std::span<std::byte, core::identity::local_ticket::kTicketSize>(body.data(), proofSize));
    }
    if (!issued) { fail("Account identity is not ready. Sign in and try again."); return 0; }
    std::ostringstream text;
    text << selected.requestId << ' ' << selected.characterSoid << ' ' << selected.weaponHash << ' ' << selected.count;
    for (std::size_t i=0; i<selected.count; ++i) text << ' ' << selected.plugs[i];
    const std::string payload = text.str();
    std::memcpy(body.data()+proofSize, payload.data(), payload.size());
    std::array<std::byte, 512> responseBytes{};
    network::HttpRequest request{};
    request.method = network::HttpMethod::post;
    request.url = "/v1/account/weapon/acquire";
    request.contentType = "application/octet-stream";
    request.body = std::span(body).first(proofSize+payload.size());
    request.response = responseBytes;
    network::HttpResponse response{};
    const bool delivered = hooks::external_server::consume(request, response);
    SecureZeroMemory(body.data(), body.size());
    if (!delivered) { fail("Server response unavailable. Retry the same roll safely."); return 0; }
    if (response.size > responseBytes.size()) { fail("Invalid server response. Retry the same roll safely."); return 0; }
    if (response.statusCode != 200) {
        const std::string_view serverError(reinterpret_cast<const char*>(responseBytes.data()), response.size);
        constexpr std::array<std::string_view,7> knownErrors{
            "Select the current character before acquiring.", "Character inventory is full.",
            "Weapon bucket is full or the roll cannot be resolved.", "Request ID already used for another roll.",
            "A perk is incompatible with its weapon socket.", "Unsupported weapon or socket count.",
            "Unable to persist weapon."};
        for (const auto message : knownErrors) {
            if (serverError == message) { fail(message.data()); return 0; }
        }
        fail(response.statusCode == 401 ? "The server rejected the account identity."
            : response.statusCode == 409 ? "Select an active character with inventory space and try again."
            : "The server rejected this weapon roll. Try another selection.");
        return 0;
    }
    std::istringstream result(std::string(reinterpret_cast<const char*>(responseBytes.data()), response.size));
    std::string marker, trailing;
    View view{};
    if (!(result >> marker >> view.characterSoid >> view.instanceSoid >> view.definitionHash >> view.revision)
        || marker != "acquired" || view.instanceSoid == 0 || view.characterSoid == 0
        || view.definitionHash != selected.weaponHash || (selected.characterSoid != 0 && view.characterSoid != selected.characterSoid)
        || (result >> trailing)) { fail("Invalid server response. Retry the same roll safely."); return 0; }
    view.status = Status::acquired;
    AcquireSRWLockExclusive(&g_lock);
    g_view = view;
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
}
void reap() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_worker && WaitForSingleObject(g_worker,0)==WAIT_OBJECT_0) {
        CloseHandle(g_worker); g_worker=nullptr;
    }
    ReleaseSRWLockExclusive(&g_lock);
}
}
bool acquire(std::uint64_t characterSoid, std::uint32_t weaponHash, std::span<const std::uint32_t> plugs) noexcept {
    if constexpr (!steam::kUniversalClientBuild) {
        fail("Weapon acquisition requires the server-connected client build.");
        return false;
    }
    if (!weaponHash || plugs.size()>12) return false;
    reap();
    AcquireSRWLockExclusive(&g_lock);
    if (g_worker) { ReleaseSRWLockExclusive(&g_lock); return false; }
    const bool retry = detail::reuse_request(g_view.status==Status::failed, g_selection,
        characterSoid, weaponHash, plugs);
    if (!retry) {
        std::uint64_t requestId{};
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&requestId), static_cast<ULONG>(sizeof(requestId)),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || requestId == 0) {
            g_selection={};
            g_view={}; g_view.status=Status::failed;
            std::snprintf(g_view.error.data(),g_view.error.size(),"Could not create the request identity. Try again.");
            ReleaseSRWLockExclusive(&g_lock); return false;
        }
        g_selection={};
        g_selection.requestId=requestId;
        g_selection.characterSoid=characterSoid; g_selection.weaponHash=weaponHash;
        g_selection.count=plugs.size();
        std::copy(plugs.begin(),plugs.end(),g_selection.plugs.begin());
    }
    g_view={}; g_view.status=Status::requesting;
    g_worker=CreateThread(nullptr,0,&worker,nullptr,0,nullptr);
    const bool started=g_worker!=nullptr;
    if (!started) { g_view.status=Status::failed; std::snprintf(g_view.error.data(),g_view.error.size(),"Could not start the request. Try again."); }
    ReleaseSRWLockExclusive(&g_lock);
    return started;
}
View snapshot() noexcept {
    reap();
    AcquireSRWLockShared(&g_lock);
    const View view=g_view;
    ReleaseSRWLockShared(&g_lock);
    return view;
}
void shutdown() noexcept {
    AcquireSRWLockShared(&g_lock); const HANDLE handle=g_worker; ReleaseSRWLockShared(&g_lock);
    if (handle) WaitForSingleObject(handle,INFINITE);
    reap();
    AcquireSRWLockExclusive(&g_lock); g_view={}; g_selection={}; ReleaseSRWLockExclusive(&g_lock);
}
}
