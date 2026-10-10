#pragma once

#include <cstdint>

#include "client/network/consumer.h"
#include "state/entitlements/definition.h"

/**
 * Launcher account requests that need Discord or Steam: expansion checks, role profiles and access
 * codes.
 *
 * Every request is a job. The HTTP handlers only validate and queue, and one worker thread does the
 * network calls: the service thread also carries gameplay, so it must never wait on Discord or
 * Steam. The launcher polls `/v1/account/jobs/result`.
 *
 * Expansion checks: the launcher posts a Discord token and a Steam web token. The worker confirms
 * the Discord user, confirms the Steam token belongs to the claimed SteamID, reads that account's
 * own licenses, and stores the result against the Discord-linked Dawn account with a one-to-one
 * SteamID pin. SignOn then withdraws the configured entitlement rows of any expansion the account
 * doesn't own. Policy lives in `<exe dir>\Restoration\expansions.json`; without it checks are off
 * and nothing is withdrawn.
 *
 * Roles and codes: capabilities come only from the bot-mirrored role mask (`roles.h`) and redeemed
 * codes, both looked up after the worker has confirmed the Discord identity.
 */
namespace restoration::expansions {

[[nodiscard]] bool initialize(void* module) noexcept;

void shutdown() noexcept;

[[nodiscard]] bool handle_submit(const dawn::client::network::HttpRequest& request,
                                 dawn::client::network::HttpResponse& response) noexcept;

[[nodiscard]] bool handle_profile(const dawn::client::network::HttpRequest& request,
                                  dawn::client::network::HttpResponse& response) noexcept;

[[nodiscard]] bool handle_issue_code(const dawn::client::network::HttpRequest& request,
                                     dawn::client::network::HttpResponse& response) noexcept;

[[nodiscard]] bool handle_redeem_code(const dawn::client::network::HttpRequest& request,
                                      dawn::client::network::HttpResponse& response) noexcept;

[[nodiscard]] bool handle_forget_access(const dawn::client::network::HttpRequest& request,
                                        dawn::client::network::HttpResponse& response) noexcept;

[[nodiscard]] bool handle_result(const dawn::client::network::HttpRequest& request,
                                 dawn::client::network::HttpResponse& response) noexcept;

[[nodiscard]] dawn::state::entitlements::Table signon_table(std::uint64_t platformId) noexcept;

} // namespace restoration::expansions
