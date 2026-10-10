#include "account_panel.h"

#include <Windows.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include "../../../core/identity/device_identity.h"
#include "../../../core/identity/local_ticket.h"
#include "../../../steam/runtime/runtime.h"
#include "../../../steam/runtime/social/realtime_client.h"
#include "../../../state/social/social_state.h"
#include "../../hooks/external_server/transport.h"

namespace dawn::client::ui::account {
namespace {

constexpr std::string_view kLinkCodePath = "/v1/account/discord-link-code";
constexpr std::size_t kCodeCapacity = 10;
constexpr std::size_t kNameCapacity = 64;
constexpr std::size_t kErrorCapacity = 160;

enum class Status : unsigned char {
    idle,
    requesting,
    ready,
    failed,
};

struct View final {
    Status status{Status::idle};
    std::array<char, kCodeCapacity> code{};
    std::array<char, kNameCapacity> accountName{};
    std::array<char, kErrorCapacity> error{};
    std::uint64_t expiresAt{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
View g_view;
HANDLE g_worker{};

[[nodiscard]] std::uint64_t now_seconds() noexcept {
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

void finish_error(std::string_view message) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_view = {};
    g_view.status = Status::failed;
    const std::size_t count = (std::min)(message.size(), g_view.error.size() - 1);
    std::copy_n(message.data(), count, g_view.error.data());
    ReleaseSRWLockExclusive(&g_lock);
}

[[nodiscard]] bool parse_u64(std::string_view text, std::uint64_t& output) noexcept {
    if (text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (value > ((std::numeric_limits<std::uint64_t>::max)() - digit) / 10U) {
            return false;
        }
        value = value * 10U + digit;
    }
    output = value;
    return true;
}

[[nodiscard]] bool parse_link_response(std::span<const std::byte> body,
                                       View& output) noexcept {
    const std::string_view text(reinterpret_cast<const char*>(body.data()), body.size());
    const std::size_t first = text.find('\n');
    const std::size_t second = first == std::string_view::npos
                                   ? std::string_view::npos
                                   : text.find('\n', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos
        || text.find('\n', second + 1) != std::string_view::npos) {
        return false;
    }
    const std::string_view code = text.substr(0, first);
    const std::string_view name = text.substr(first + 1, second - first - 1);
    const std::string_view expiry = text.substr(second + 1);
    if (code.empty() || code.size() >= output.code.size() || name.empty()
        || name.size() >= output.accountName.size()
        || !parse_u64(expiry, output.expiresAt)) {
        return false;
    }
    std::copy(code.begin(), code.end(), output.code.begin());
    std::copy(name.begin(), name.end(), output.accountName.begin());
    output.status = Status::ready;
    return true;
}

DWORD WINAPI request_worker([[maybe_unused]] void* context) noexcept {
    const std::uint64_t now = now_seconds();
    std::array<std::byte, core::identity::device_identity::kProofSize> proof{};
    std::size_t proofSize = 0;
    bool issued = false;
    if constexpr (steam::kUniversalClientBuild) {
        proofSize = core::identity::device_identity::kProofSize;
        issued = core::identity::device_identity::issue(
            core::identity::device_identity::Scope::discordLinkCode,
            now,
            std::span<std::byte, core::identity::device_identity::kProofSize>(proof));
    } else {
        proofSize = core::identity::local_ticket::kTicketSize;
        issued = core::identity::local_ticket::issue(
            steam::local_steam_id(),
            now,
            std::span<std::byte, core::identity::local_ticket::kTicketSize>(
                proof.data(), core::identity::local_ticket::kTicketSize));
    }
    if (now == 0 || !issued) {
        finish_error("The local account identity is not ready. Sign in, then try again.");
        return 0;
    }

    std::array<std::byte, 512> responseBody{};
    client::network::HttpRequest request{};
    request.method = client::network::HttpMethod::post;
    request.url = kLinkCodePath;
    request.contentType = "application/octet-stream";
    request.body = std::span(proof).first(proofSize);
    request.response = responseBody;
    client::network::HttpResponse response{};
    const bool transported = client::hooks::external_server::consume(request, response);
    SecureZeroMemory(proof.data(), proof.size());
    if (!transported) {
        finish_error("The Restoration server is not reachable.");
        return 0;
    }
    if (response.statusCode != 200) {
        finish_error(response.statusCode == 401
                         ? "The server rejected the local account proof. Restart both programs."
                         : "The server could not create a link code.");
        return 0;
    }

    View ready{};
    if (!parse_link_response(
            std::span<const std::byte>(responseBody.data(), response.size), ready)) {
        finish_error("The server returned an invalid link-code response.");
        return 0;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_view = ready;
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
}

void reap_worker() noexcept {
    HANDLE completed = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    if (g_worker != nullptr && WaitForSingleObject(g_worker, 0) == WAIT_OBJECT_0) {
        completed = g_worker;
        g_worker = nullptr;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (completed != nullptr) {
        (void)CloseHandle(completed);
    }
}

void request_code() noexcept {
    reap_worker();
    AcquireSRWLockExclusive(&g_lock);
    if (g_worker != nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    g_view = {};
    g_view.status = Status::requesting;
    g_worker = CreateThread(nullptr, 0, &request_worker, nullptr, 0, nullptr);
    if (g_worker == nullptr) {
        g_view.status = Status::failed;
        constexpr std::string_view failure = "Windows could not start the link-code request.";
        std::copy(failure.begin(), failure.end(), g_view.error.begin());
    }
    ReleaseSRWLockExclusive(&g_lock);
}

[[nodiscard]] View snapshot() noexcept {
    AcquireSRWLockShared(&g_lock);
    const View result = g_view;
    ReleaseSRWLockShared(&g_lock);
    return result;
}

} // namespace

void draw() noexcept {
    reap_worker();
    const View view = snapshot();

    namespace realtime = steam::runtime::social::realtime_client;
    const realtime::PendingInvite pending = realtime::pending_invite_snapshot();
    if (pending.active) {
        std::array<char, state::social::kNameCapacity> senderName{};
        const state::social::Snapshot social = state::social::snapshot();
        for (std::size_t index = 0; index < social.remoteCount; ++index) {
            if (social.remotes[index].platformId == pending.senderPlatformId) {
                senderName = social.remotes[index].displayName;
                break;
            }
        }
        ImGui::TextUnformatted("Fireteam Invitation");
        ImGui::Separator();
        if (senderName.front() != '\0') {
            ImGui::Text("From: %s", senderName.data());
        } else {
            ImGui::Text("From Steam account: %llu",
                        static_cast<unsigned long long>(pending.senderPlatformId));
        }
        if (ImGui::Button("Accept Fireteam Invite")) {
            (void)realtime::accept_invite();
        }
        ImGui::SameLine();
        if (ImGui::Button("Decline")) {
            realtime::decline_invite();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::TextUnformatted("Discord Account Link");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Generate a private one-time code here, then use /account link in the Restoration "
        "Discord. The code expires after five minutes and can only be used once.");
    ImGui::Spacing();

    if (view.status != Status::requesting && ImGui::Button("Generate Link Code")) {
        request_code();
    }
    if (view.status == Status::requesting) {
        ImGui::TextUnformatted("Requesting a code from the local server...");
    } else if (view.status == Status::failed) {
        ImGui::TextWrapped("%s", view.error.data());
    } else if (view.status == Status::ready) {
        const std::uint64_t now = now_seconds();
        const std::uint64_t remaining = view.expiresAt > now ? view.expiresAt - now : 0;
        ImGui::Spacing();
        ImGui::Text("Account: %s", view.accountName.data());
        ImGui::Text("Link code: %s", view.code.data());
        ImGui::Text("Expires in: %llu seconds",
                    static_cast<unsigned long long>(remaining));
        if (remaining != 0 && ImGui::Button("Copy Code")) {
            ImGui::SetClipboardText(view.code.data());
        }
        if (remaining == 0) {
            ImGui::TextUnformatted("This code has expired. Generate a new one.");
        }
    }
}

void shutdown() noexcept {
    HANDLE worker = nullptr;
    AcquireSRWLockExclusive(&g_lock);
    worker = g_worker;
    ReleaseSRWLockExclusive(&g_lock);
    if (worker != nullptr) {
        (void)WaitForSingleObject(worker, INFINITE);
    }
    reap_worker();
    AcquireSRWLockExclusive(&g_lock);
    g_view = {};
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace dawn::client::ui::account
