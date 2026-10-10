#include "expansions.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "account_store.h"
#include "core/filesystem/path.h"
#include "core/logging/log.h"
#include "https_client.h"
#include "roles.h"
#include "state/build_data/collectibles/collectible_catalog.h"
#include "state/build_data/items/item_catalog.h"
#include "state/entitlements/entitlement_runtime.h"

namespace restoration::expansions {
namespace {

using dawn::client::network::HttpRequest;
using dawn::client::network::HttpResponse;
namespace entitlements = dawn::state::entitlements;

constexpr unsigned kHttpOk = 200;
constexpr unsigned kHttpAccepted = 202;
constexpr unsigned kHttpBadRequest = 400;
constexpr unsigned kHttpUnauthorized = 401;
constexpr unsigned kHttpForbidden = 403;
constexpr unsigned kHttpNotFound = 404;
constexpr unsigned kHttpConflict = 409;
constexpr unsigned kHttpBadGateway = 502;
constexpr unsigned kHttpUnavailable = 503;

constexpr std::wstring_view kConfigSuffix = L"\\expansions.json";
constexpr std::size_t kConfigCapacity = 64 * 1024;
constexpr std::uint32_t kDestinyApp = 1085660;
constexpr std::uint32_t kForsakenApp = 1090150;
constexpr std::uint32_t kShadowkeepApp = 1090200;

// pre-order, deluxe and expansion-pass packs were separate Steam apps, so owning one proves an
// early purchase and earns its bonus rewards; reported to the launcher, unlocks nothing
constexpr std::uint32_t kBonusApps[] = {
    1090090,
    1090092,
    1090094,
    1090095,
    1090096,
    1090151,
    1090152,
    1090170,
    1090171,
    1090201,
    1090202,
};


constexpr std::uint64_t kSteamIndividualPrefix = 0x0110000100000000ULL;
constexpr std::size_t kJobCapacity = 32;
constexpr std::uint64_t kJobLifetimeSeconds = 120;
constexpr std::size_t kNameListCapacity = 16;
constexpr std::size_t kResultCapacity = 2048;
constexpr std::size_t kJobIdSize = 32;
constexpr std::size_t kMaximumProviderToken = 512;
constexpr std::size_t kMaximumSteamToken = 4096;
constexpr std::size_t kCodeSize = 16;
constexpr std::string_view kCodeAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
constexpr DWORD kWorkerStopMilliseconds = 60000;

using Name = std::array<char, entitlements::kNameCapacity>;

struct NameList final {
    std::array<Name, kNameListCapacity> names{};
    std::size_t count{};
};

struct Config final {
    bool enabled{};
    NameList forsaken{};
    NameList shadowkeep{};
};

enum class JobState : std::uint8_t {
    empty,
    queued,
    running,
    done,
};

enum class JobKind : std::uint8_t {
    expansion,
    profile,
    issueCode,
    redeemCode,
    forgetAccess,
};

struct Input final {
    JobKind kind{};
    std::string providerToken;
    std::string steamToken;
    std::string code;
    std::array<char, 21> steamIdText{};
    std::uint64_t steamId{};
    std::array<char, account_store::kAccountNameCapacity> accountName{};
    std::uint32_t grants{};
};

struct Job final {
    JobState state{};
    std::array<char, kJobIdSize + 1> id{};
    Input input{};
    unsigned status{};
    std::array<char, kResultCapacity> body{};
    std::size_t bodySize{};
    std::uint64_t expiresAt{};
};

struct Outcome final {
    unsigned status{};
    std::array<char, kResultCapacity> body{};
    std::size_t bodySize{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
CONDITION_VARIABLE g_wake{CONDITION_VARIABLE_INIT};
std::array<Job, kJobCapacity> g_jobs{};
Config g_config{};
HANDLE g_worker{};
bool g_stopping{};
bool g_workerRunning{};

[[nodiscard]] std::uint64_t now_seconds() noexcept {
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

void log_line(dawn::core::log::Level level, const char* format, ...) noexcept {
    std::array<char, dawn::core::log::kLineCapacity> line{};
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(line.data(), line.size(), format, arguments);
    va_end(arguments);
    if (written > 0) {
        dawn::core::log::write(
            dawn::core::log::Channel::server,
            level,
            {line.data(), std::min(static_cast<std::size_t>(written), line.size() - 1)});
    }
}

// hashes come from the public item database (2026-09-20) and are candidates until
// report_bonus_rewards confirms them against the installed packages
struct BonusReward final {
    std::string_view pack;
    std::string_view name;
    std::uint32_t definitionHash;
};

// the Kill-Tracker Ghost is left out on purpose: Shaxx already sells it
constexpr BonusReward kBonusRewards[] = {
    // both emotes exist twice in the manifest (Red War and Warmind); these are the Warmind rows
    {"expansion-pass", "Salute", 646486611U},
    {"expansion-pass", "Shadow Dance", 3163467796U},
    {"expansion-pass", "Emperor's Shadows", 1940590817U},
    {"forsaken", "Last Hand", 2243729317U},
    {"forsaken", "Queen of Hearts", 1673638927U},
    {"forsaken", "Standoff", 1617583310U},
    {"forsaken", "Cayde's Duds", 89332041U},
    {"forsaken", "Wrath Majestic", 2241160198U},
    {"forsaken", "Vestian Shell", 3773306894U},
    {"forsaken", "The Forsaken", 2133500862U},
    {"shadowkeep", "Two-Tailed Fox", 2694576561U},
    {"shadowkeep", "Chasing Kitsune", 2982346244U},
    {"shadowkeep", "Hearing Whispers", 2414093918U},
    {"shadowkeep", "Eris Morn Shell", 2857908936U},
    {"shadowkeep", "Benighted Omen", 3605490917U},
};

LONG g_bonusRewardsReported = 0;

void report_bonus_rewards() noexcept {
    if (InterlockedExchange(&g_bonusRewardsReported, 1) != 0) {
        return;
    }
    namespace items = dawn::state::build_data::items;
    namespace collectibles = dawn::state::build_data::collectibles;

    const std::size_t collectibleCount = collectibles::count();
    log_line(dawn::core::log::Level::info,
             "ev=preorder stage=resolve_begin rewards=%zu items=%zu collectibles=%zu",
             std::size(kBonusRewards),
             items::count(),
             collectibleCount);

    for (const BonusReward& reward : kBonusRewards) {
        items::Definition item{};
        if (!items::find_hash(reward.definitionHash, item)) {
            log_line(dawn::core::log::Level::warn,
                     "ev=preorder stage=resolve result=no_item pack=%.*s name=%.*s hash=0x%08X",
                     static_cast<int>(reward.pack.size()),
                     reward.pack.data(),
                     static_cast<int>(reward.name.size()),
                     reward.name.data(),
                     reward.definitionHash);
            continue;
        }

        std::uint16_t collectibleIndex = collectibles::kUnavailableItemDefinitionIndex;
        const bool grantable = collectibles::grants_item(item.definitionIndex);
        for (std::size_t index = 0; index < collectibleCount; ++index) {
            collectibles::Definition row{};
            if (collectibles::find(static_cast<std::uint16_t>(index), row)
                && row.itemDefinitionIndex == item.definitionIndex) {
                collectibleIndex = row.collectibleIndex;
                break;
            }
        }
        log_line(collectibleIndex == collectibles::kUnavailableItemDefinitionIndex
                     ? dawn::core::log::Level::warn
                     : dawn::core::log::Level::info,
                 "ev=preorder stage=resolve result=%s pack=%.*s name=%.*s hash=0x%08X "
                 "item_index=%u bucket=%u grantable=%d collectible_index=%d",
                 collectibleIndex == collectibles::kUnavailableItemDefinitionIndex ? "no_collectible"
                                                                                  : "ok",
                 static_cast<int>(reward.pack.size()),
                 reward.pack.data(),
                 static_cast<int>(reward.name.size()),
                 reward.name.data(),
                 reward.definitionHash,
                 static_cast<unsigned>(item.definitionIndex),
                 static_cast<unsigned>(item.bucketId),
                 grantable ? 1 : 0,
                 collectibleIndex == collectibles::kUnavailableItemDefinitionIndex
                     ? -1
                     : static_cast<int>(collectibleIndex));
    }
    log_line(dawn::core::log::Level::info, "ev=preorder stage=resolve_end");
}

void wipe(std::string& secret) noexcept {
    if (!secret.empty()) {
        SecureZeroMemory(secret.data(), secret.size());
    }
    secret.clear();
    secret.shrink_to_fit();
}

void wipe(Input& input) noexcept {
    wipe(input.providerToken);
    wipe(input.steamToken);
    wipe(input.code);
}

void set_name(NameList& list, std::string_view name) noexcept {
    if (list.count >= list.names.size() || name.empty() || name.size() >= entitlements::kNameCapacity) {
        return;
    }
    Name& slot = list.names[list.count++];
    slot.fill('\0');
    std::copy(name.begin(), name.end(), slot.begin());
}

[[nodiscard]] Config default_config() noexcept {
    Config config{};
    for (const std::string_view name : {"1090150", "1090151", "1090152", "1090170", "1090171"}) {
        set_name(config.forsaken, name);
    }
    for (const std::string_view name : {"1090200", "1090201", "1090202"}) {
        set_name(config.shadowkeep, name);
    }
    return config;
}

[[nodiscard]] bool in_list(const NameList& list, std::string_view name) noexcept {
    for (std::size_t index = 0; index < list.count; ++index) {
        if (std::string_view(list.names[index].data()) == name) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t skip_space(std::string_view text, std::size_t at) noexcept {
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) {
        ++at;
    }
    return at;
}

[[nodiscard]] std::size_t value_of(std::string_view text, std::string_view key) noexcept {
    std::string quoted;
    quoted.reserve(key.size() + 2);
    quoted.push_back('"');
    quoted.append(key);
    quoted.push_back('"');
    for (std::size_t found = text.find(quoted); found != std::string_view::npos;
         found = text.find(quoted, found + 1)) {
        const std::size_t colon = skip_space(text, found + quoted.size());
        if (colon < text.size() && text[colon] == ':') {
            return skip_space(text, colon + 1);
        }
    }
    return std::string_view::npos;
}

[[nodiscard]] bool read_string(std::string_view text,
                               std::size_t& at,
                               std::string& output,
                               std::size_t limit) noexcept {
    output.clear();
    if (at >= text.size() || text[at] != '"') {
        return false;
    }
    for (++at; at < text.size(); ++at) {
        char character = text[at];
        if (character == '"') {
            ++at;
            return true;
        }
        if (static_cast<unsigned char>(character) < 0x20 || output.size() >= limit) {
            return false;
        }
        if (character == '\\') {
            if (++at >= text.size()) {
                return false;
            }
            switch (text[at]) {
            case '"': character = '"'; break;
            case '\\': character = '\\'; break;
            case '/': character = '/'; break;
            case 'n': character = '\n'; break;
            case 't': character = '\t'; break;
            case 'u': {
                if (at + 4 >= text.size()) {
                    return false;
                }
                unsigned value = 0;
                for (std::size_t digit = 1; digit <= 4; ++digit) {
                    const char hex = text[at + digit];
                    value <<= 4;
                    if (hex >= '0' && hex <= '9') value |= static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') value |= static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') value |= static_cast<unsigned>(hex - 'A' + 10);
                    else return false;
                }
                if (value > 0x7F) {
                    return false;
                }
                character = static_cast<char>(value);
                at += 4;
                break;
            }
            default:
                return false;
            }
        }
        output.push_back(character);
    }
    return false;
}

[[nodiscard]] bool json_string(std::string_view text,
                               std::string_view key,
                               std::string& output,
                               std::size_t limit) noexcept {
    std::size_t at = value_of(text, key);
    return at != std::string_view::npos && read_string(text, at, output, limit);
}

[[nodiscard]] bool json_unsigned(std::string_view text, std::string_view key, std::uint64_t& output) noexcept {
    output = 0;
    std::size_t at = value_of(text, key);
    if (at == std::string_view::npos || at >= text.size() || text[at] < '0' || text[at] > '9') {
        return false;
    }
    for (; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at) {
        const std::uint64_t digit = static_cast<std::uint64_t>(text[at] - '0');
        if (output > (UINT64_MAX - digit) / 10) {
            return false;
        }
        output = output * 10 + digit;
    }
    return true;
}

[[nodiscard]] bool json_bool(std::string_view text, std::string_view key, bool& output) noexcept {
    const std::size_t at = value_of(text, key);
    if (at == std::string_view::npos) {
        return false;
    }
    if (text.substr(at, 4) == "true") {
        output = true;
        return true;
    }
    if (text.substr(at, 5) == "false") {
        output = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool json_names(std::string_view text, std::string_view key, NameList& output) noexcept {
    std::size_t at = value_of(text, key);
    if (at == std::string_view::npos || at >= text.size() || text[at] != '[') {
        return false;
    }
    output = {};
    at = skip_space(text, at + 1);
    if (at < text.size() && text[at] == ']') {
        return true;
    }
    std::string name;
    for (;;) {
        if (!read_string(text, at, name, entitlements::kNameCapacity - 1) || name.empty()
            || output.count >= output.names.size()) {
            return false;
        }
        set_name(output, name);
        at = skip_space(text, at);
        if (at < text.size() && text[at] == ']') {
            return true;
        }
        if (at >= text.size() || text[at] != ',') {
            return false;
        }
        at = skip_space(text, at + 1);
    }
}

// returns false when the array is missing, so absence is never read as not owned
[[nodiscard]] bool json_integer_array(std::string_view text,
                                      std::string_view key,
                                      std::uint32_t wanted,
                                      bool& contains,
                                      std::size_t& count) noexcept {
    contains = false;
    count = 0;
    std::size_t at = value_of(text, key);
    if (at == std::string_view::npos || at >= text.size() || text[at] != '[') {
        return false;
    }
    at = skip_space(text, at + 1);
    while (at < text.size() && text[at] != ']') {
        std::uint64_t value = 0;
        std::size_t digits = 0;
        for (; at < text.size() && text[at] >= '0' && text[at] <= '9'; ++at, ++digits) {
            value = value * 10 + static_cast<std::uint64_t>(text[at] - '0');
            if (digits > 10) {
                return false;
            }
        }
        if (digits == 0) {
            return false;
        }
        ++count;
        contains = contains || value == wanted;
        at = skip_space(text, at);
        if (at < text.size() && text[at] == ',') {
            at = skip_space(text, at + 1);
        }
    }
    return at < text.size();
}

[[nodiscard]] bool base64url_decode(std::string_view input, std::string& output) noexcept {
    output.clear();
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char character : input) {
        int value;
        if (character >= 'A' && character <= 'Z') value = character - 'A';
        else if (character >= 'a' && character <= 'z') value = character - 'a' + 26;
        else if (character >= '0' && character <= '9') value = character - '0' + 52;
        else if (character == '-' || character == '+') value = 62;
        else if (character == '_' || character == '/') value = 63;
        else if (character == '=') break;
        else return false;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return !output.empty();
}

[[nodiscard]] bool decimal(std::string_view text, std::uint64_t& output) noexcept {
    output = 0;
    if (text.empty() || text.size() > 20) {
        return false;
    }
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
        if (output > (UINT64_MAX - digit) / 10) {
            return false;
        }
        output = output * 10 + digit;
    }
    return true;
}

[[nodiscard]] bool printable_token(std::string_view token, std::size_t minimum, std::size_t maximum) noexcept {
    return token.size() >= minimum && token.size() <= maximum
           && std::all_of(token.begin(), token.end(), [](char character) noexcept {
                  const auto byte = static_cast<unsigned char>(character);
                  return byte > 0x20 && byte < 0x7F && byte != '"' && byte != '\\';
              });
}

[[nodiscard]] bool sha256(std::string_view input, std::array<std::byte, 32>& output) noexcept {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    const bool success =
        BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
        && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0
        && BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())), static_cast<ULONG>(input.size()), 0) >= 0
        && BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(output.data()), static_cast<ULONG>(output.size()), 0) >= 0;
    if (hash != nullptr) (void)BCryptDestroyHash(hash);
    if (algorithm != nullptr) (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    return success;
}

[[nodiscard]] Outcome error(unsigned status, const char* code, const char* message) noexcept {
    Outcome outcome{};
    outcome.status = status;
    const int written = std::snprintf(outcome.body.data(),
                                      outcome.body.size(),
                                      "{\"error\":\"%s\",\"message\":\"%s\"}",
                                      code,
                                      message);
    outcome.bodySize = written > 0 ? std::min(static_cast<std::size_t>(written), outcome.body.size() - 1) : 0;
    return outcome;
}

[[nodiscard]] Outcome json_outcome(unsigned status, const std::string& body) noexcept {
    if (body.size() >= kResultCapacity) {
        return error(kHttpUnavailable, "server_error", "The answer was too large.");
    }
    Outcome outcome{};
    outcome.status = status;
    std::memcpy(outcome.body.data(), body.data(), body.size());
    outcome.bodySize = body.size();
    return outcome;
}

void write_json(const HttpRequest& request, HttpResponse& response, unsigned status, std::string_view body) noexcept {
    response.contentType = "application/json";
    if (body.size() > request.response.size()) {
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        return;
    }
    std::memcpy(request.response.data(), body.data(), body.size());
    response.statusCode = status;
    response.size = body.size();
}

void write_outcome(const HttpRequest& request, HttpResponse& response, const Outcome& outcome) noexcept {
    write_json(request, response, outcome.status, {outcome.body.data(), outcome.bodySize});
}

[[nodiscard]] bool header_line(std::wstring_view name, std::string_view value, std::wstring& output) noexcept {
    std::wstring widened;
    if (!https_client::widen_ascii(value, widened)) {
        return false;
    }
    output.assign(name);
    output.append(widened);
    output.append(L"\r\n");
    SecureZeroMemory(widened.data(), widened.size() * sizeof(wchar_t));
    return true;
}

[[nodiscard]] bool discord_identity(const std::string& token, std::string& discordUserId, Outcome& failure) noexcept {
    std::wstring headers;
    if (!header_line(L"Authorization: Bearer ", token, headers)) {
        failure = error(kHttpBadRequest, "bad_request", "The request was malformed.");
        return false;
    }
    https_client::Response discord{};
    const bool reached = https_client::get(L"discord.com", L"/api/v10/users/@me", headers, discord);
    SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
    if (!reached) {
        failure = error(kHttpBadGateway, "discord_unavailable", "Couldn't reach Discord. Try again.");
        return false;
    }
    if (discord.status == 401) {
        failure = error(kHttpUnauthorized, "discord_token_rejected", "Discord didn't accept your session. Sign in again.");
        return false;
    }
    std::uint64_t numeric = 0;
    if (discord.status != 200 || !json_string(discord.body, "id", discordUserId, 20) || !decimal(discordUserId, numeric)) {
        log_line(dawn::core::log::Level::warn, "ev=launcher_api stage=discord result=fail status=%u", discord.status);
        failure = error(kHttpBadGateway, "discord_unavailable", "Discord answered unexpectedly. Try again.");
        return false;
    }
    return true;
}

[[nodiscard]] Outcome verify_expansion(Input& input, const std::string& discordUserId) noexcept {
    const std::string_view steamIdText(input.steamIdText.data());

    const std::size_t firstDot = input.steamToken.find('.');
    const std::size_t secondDot = firstDot == std::string::npos ? std::string::npos : input.steamToken.find('.', firstDot + 1);
    std::string claims;
    std::string subject;
    std::uint64_t expiresAt = 0;
    if (secondDot == std::string::npos
        || !base64url_decode(std::string_view(input.steamToken).substr(firstDot + 1, secondDot - firstDot - 1), claims)
        || !json_string(claims, "sub", subject, 20) || subject != steamIdText
        || !json_unsigned(claims, "exp", expiresAt) || expiresAt <= now_seconds()) {
        return error(kHttpUnauthorized, "steam_token_rejected", "Steam didn't accept the session. Sign in to Steam again.");
    }

    std::wstring steamPath;
    if (!https_client::widen_ascii("/IPlayerService/GetOwnedGames/v1/?include_played_free_games=true&access_token="
                                       + input.steamToken + "&steamid=" + std::string(steamIdText),
                                   steamPath)) {
        return error(kHttpBadRequest, "bad_request", "The request was malformed.");
    }
    https_client::Response owned{};
    const bool steamReached = https_client::get(L"api.steampowered.com", steamPath, L"", owned);
    SecureZeroMemory(steamPath.data(), steamPath.size() * sizeof(wchar_t));
    if (!steamReached) {
        return error(kHttpBadGateway, "steam_unavailable", "Couldn't reach Steam. Try again.");
    }
    if (owned.status == 401 || owned.status == 403) {
        return error(kHttpUnauthorized, "steam_token_rejected", "Steam didn't accept the session. Sign in to Steam again.");
    }
    if (owned.status != 200 || owned.body.find("\"response\"") == std::string::npos) {
        log_line(dawn::core::log::Level::warn, "ev=expansions stage=steam_token result=fail status=%u", owned.status);
        return error(kHttpBadGateway, "steam_unavailable", "Steam answered unexpectedly. Try again.");
    }

    // Ownership from the account's own licenses. Destiny 2 itself is the control: a library that
    // doesn't list it wasn't really read, so it can't be taken as "doesn't own the expansions".
    std::wstring cookie;
    std::string cookieValue = std::string(steamIdText) + "%7C%7C" + input.steamToken;
    const bool cookieBuilt = header_line(L"Cookie: steamLoginSecure=", cookieValue, cookie);
    wipe(cookieValue);
    if (!cookieBuilt) {
        return error(kHttpBadRequest, "bad_request", "The request was malformed.");
    }
    https_client::Response store{};
    const bool storeReached = https_client::get(L"store.steampowered.com", L"/dynamicstore/userdata/", cookie, store);
    SecureZeroMemory(cookie.data(), cookie.size() * sizeof(wchar_t));
    bool hasDestiny = false;
    bool forsaken = false;
    bool shadowkeep = false;
    std::size_t appCount = 0;
    const bool libraryRead = storeReached && store.status == 200
                             && json_integer_array(store.body, "rgOwnedApps", kDestinyApp, hasDestiny, appCount)
                             && json_integer_array(store.body, "rgOwnedApps", kForsakenApp, forsaken, appCount)
                             && json_integer_array(store.body, "rgOwnedApps", kShadowkeepApp, shadowkeep, appCount);
    // bonus packs come from the same library answer. Pre-order and deluxe packs contain the
    // expansion, so owning one counts as owning it. The Annual Pass is seasons only, not listed
    std::string packs;
    if (libraryRead) {
        for (const std::uint32_t app : kBonusApps) {
            bool ownsPack = false;
            std::size_t ignored = 0;
            if (json_integer_array(store.body, "rgOwnedApps", app, ownsPack, ignored) && ownsPack) {
                if (!packs.empty()) {
                    packs += ',';
                }
                packs += '"';
                packs += std::to_string(app);
                packs += '"';
                forsaken = forsaken || app == 1090152 || app == 1090170 || app == 1090171;
                shadowkeep = shadowkeep || app == 1090201 || app == 1090202;
            }
        }
    }
    log_line(dawn::core::log::Level::info,
             "ev=expansions stage=library result=%s status=%u apps=%zu destiny=%d forsaken=%d shadowkeep=%d packs=%s",
             libraryRead && hasDestiny ? "ok" : "unverified",
             store.status,
             appCount,
             hasDestiny ? 1 : 0,
             forsaken ? 1 : 0,
             shadowkeep ? 1 : 0,
             packs.empty() ? "none" : packs.c_str());
    if (!libraryRead || !hasDestiny) {
        return error(kHttpBadGateway,
                     "steam_ownership_unverified",
                     "Steam didn't return your library. Make sure Destiny 2 is in your Steam library, then try again.");
    }

    std::uint64_t pinned = 0;
    const account_store::ExpansionRecord recorded = account_store::record_expansion_check(
        discordUserId, input.steamId, std::string_view(input.accountName.data()), forsaken, shadowkeep, now_seconds(), pinned);
    switch (recorded) {
    case account_store::ExpansionRecord::ok: {
        std::string body = "{\"steamId\":\"" + std::string(steamIdText) + "\",\"forsaken\":"
                           + (forsaken ? "true" : "false") + ",\"shadowkeep\":" + (shadowkeep ? "true" : "false")
                           + ",\"packs\":[" + packs + "]"
                           + ",\"checkedAt\":" + std::to_string(now_seconds()) + "}";
        log_line(dawn::core::log::Level::info,
                 "ev=expansions stage=record result=ok forsaken=%d shadowkeep=%d",
                 forsaken ? 1 : 0,
                 shadowkeep ? 1 : 0);
        return json_outcome(kHttpOk, body);
    }
    case account_store::ExpansionRecord::notLinked:
        return error(kHttpForbidden, "account_not_linked", "Link your Dawn account to Discord first.");
    case account_store::ExpansionRecord::steamMismatch:
        return error(kHttpConflict, "steam_account_mismatch", "This Steam account isn't the one linked to your Dawn account.");
    case account_store::ExpansionRecord::steamInUse:
        return error(kHttpConflict, "steam_account_in_use", "That Steam account already unlocks another Dawn account.");
    case account_store::ExpansionRecord::failed:
        break;
    }
    return error(kHttpUnavailable, "server_error", "The server couldn't save the result. Try again.");
}

[[nodiscard]] Outcome profile(const std::string& discordUserId) noexcept {
    const std::uint32_t roleMask = account_store::discord_role_mask(discordUserId);
    const std::uint32_t grants = account_store::discord_access_grants(discordUserId) & roles::kGrantable;
    std::array<char, account_store::kAccountNameCapacity> linked{};
    const bool isLinked = account_store::discord_linked_account(discordUserId, linked);

    std::string body = "{\"discordUserId\":";
    roles::append_json_string(body, discordUserId);
    body += ",\"badges\":";
    roles::append_badges_json(roleMask, body);
    body += ",\"capabilities\":";
    roles::append_capabilities_json(roles::capabilities(roleMask) | grants, body);
    body += ",\"grants\":";
    roles::append_capabilities_json(grants, body);
    body += ",\"linkedAccount\":";
    if (isLinked) {
        roles::append_json_string(body, linked.data());
    } else {
        body += "null";
    }
    body.push_back('}');
    return json_outcome(kHttpOk, body);
}

[[nodiscard]] bool code_hash(std::string_view normalized, std::array<std::byte, 32>& output) noexcept {
    return sha256("dawn-code:" + std::string(normalized), output);
}

[[nodiscard]] Outcome issue_code(const std::string& discordUserId, std::uint32_t grants) noexcept {
    const std::uint32_t allowed = roles::capabilities(account_store::discord_role_mask(discordUserId));
    if ((allowed & roles::kIssueCodes) == 0) {
        return error(kHttpForbidden, "not_allowed", "Your Discord roles don't allow creating codes.");
    }
    if (grants == 0 || (grants & ~roles::kGrantable) != 0) {
        return error(kHttpBadRequest, "bad_request", "Pick what the code unlocks.");
    }
    std::array<unsigned char, kCodeSize> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        return error(kHttpUnavailable, "server_error", "The server couldn't create a code. Try again.");
    }
    // 256 is a multiple of 32, so this is unbiased: 80 bits of entropy
    std::string raw(kCodeSize, '0');
    for (std::size_t index = 0; index < kCodeSize; ++index) {
        raw[index] = kCodeAlphabet[random[index] % kCodeAlphabet.size()];
    }
    SecureZeroMemory(random.data(), random.size());
    std::array<std::byte, 32> hash{};
    if (!code_hash(raw, hash) || !account_store::create_access_code(hash, grants, discordUserId, now_seconds())) {
        wipe(raw);
        return error(kHttpUnavailable, "server_error", "The server couldn't save the code. Try again.");
    }
    std::string body = "{\"code\":\"" + raw.substr(0, 4) + "-" + raw.substr(4, 4) + "-" + raw.substr(8, 4) + "-"
                       + raw.substr(12, 4) + "\",\"grants\":";
    roles::append_capabilities_json(grants, body);
    body.push_back('}');
    wipe(raw);
    log_line(dawn::core::log::Level::info, "ev=access_code stage=issue result=ok grants=%u", grants);
    return json_outcome(kHttpOk, body);
}

[[nodiscard]] Outcome redeem_code(const std::string& discordUserId, const std::string& code) noexcept {
    std::string normalized;
    for (const char character : code) {
        if ((character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z')) {
            normalized.push_back(character);
        } else if (character >= 'a' && character <= 'z') {
            normalized.push_back(static_cast<char>(character - 'a' + 'A'));
        }
    }
    std::array<std::byte, 32> hash{};
    if (normalized.size() != kCodeSize || !code_hash(normalized, hash)) {
        wipe(normalized);
        return error(kHttpNotFound, "invalid_code", "That code isn't valid.");
    }
    wipe(normalized);
    std::uint32_t grants = 0;
    switch (account_store::redeem_access_code(hash, discordUserId, now_seconds(), grants)) {
    case account_store::AccessCodeRedeem::ok: {
        std::string body = "{\"grants\":";
        roles::append_capabilities_json(grants & roles::kGrantable, body);
        body.push_back('}');
        return json_outcome(kHttpOk, body);
    }
    case account_store::AccessCodeRedeem::notFound:
        return error(kHttpNotFound, "invalid_code", "That code isn't valid.");
    case account_store::AccessCodeRedeem::alreadyUsed:
        return error(kHttpConflict, "code_used", "That code has already been used.");
    case account_store::AccessCodeRedeem::failed:
        break;
    }
    return error(kHttpUnavailable, "server_error", "The server couldn't redeem the code. Try again.");
}

[[nodiscard]] Outcome run(Input& input) noexcept {
    std::string discordUserId;
    Outcome failure{};
    if (!discord_identity(input.providerToken, discordUserId, failure)) {
        return failure;
    }
    switch (input.kind) {
    case JobKind::expansion:
        return verify_expansion(input, discordUserId);
    case JobKind::profile:
        return profile(discordUserId);
    case JobKind::issueCode:
        return issue_code(discordUserId, input.grants);
    case JobKind::redeemCode:
        return redeem_code(discordUserId, input.code);
    case JobKind::forgetAccess:
        if (!account_store::clear_discord_access_grants(discordUserId)) {
            return error(kHttpUnavailable, "server_error", "The server couldn't remove the access. Try again.");
        }
        return json_outcome(kHttpOk, "{\"grants\":[]}");
    }
    return error(kHttpBadRequest, "bad_request", "The request was malformed.");
}

void sweep_locked(std::uint64_t now) noexcept {
    for (Job& job : g_jobs) {
        if (job.state == JobState::done && job.expiresAt <= now) {
            job = Job{};
        }
    }
}

[[nodiscard]] Job* find_locked(std::string_view id) noexcept {
    for (Job& job : g_jobs) {
        if (job.state != JobState::empty && std::string_view(job.id.data()) == id) {
            return &job;
        }
    }
    return nullptr;
}

[[nodiscard]] bool new_job_id(std::array<char, kJobIdSize + 1>& output) noexcept {
    std::array<unsigned char, kJobIdSize / 2> random{};
    if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) {
        return false;
    }
    constexpr std::string_view kHex = "0123456789abcdef";
    for (std::size_t index = 0; index < random.size(); ++index) {
        output[index * 2] = kHex[random[index] >> 4];
        output[index * 2 + 1] = kHex[random[index] & 0x0F];
    }
    output[kJobIdSize] = '\0';
    return true;
}

DWORD WINAPI worker(void*) noexcept {
    for (;;) {
        AcquireSRWLockExclusive(&g_lock);
        Job* job = nullptr;
        for (;;) {
            if (g_stopping) {
                ReleaseSRWLockExclusive(&g_lock);
                return 0;
            }
            for (Job& candidate : g_jobs) {
                if (candidate.state == JobState::queued) {
                    job = &candidate;
                    break;
                }
            }
            if (job != nullptr) {
                break;
            }
            (void)SleepConditionVariableSRW(&g_wake, &g_lock, INFINITE, 0);
        }
        job->state = JobState::running;
        Input input = std::move(job->input);
        job->input = Input{};
        const std::array<char, kJobIdSize + 1> id = job->id;
        ReleaseSRWLockExclusive(&g_lock);

        const Outcome outcome = run(input);
        wipe(input);

        AcquireSRWLockExclusive(&g_lock);
        if (Job* finished = find_locked(id.data()); finished != nullptr) {
            finished->status = outcome.status;
            finished->body = outcome.body;
            finished->bodySize = outcome.bodySize;
            finished->state = JobState::done;
            finished->expiresAt = now_seconds() + kJobLifetimeSeconds;
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
}

void queue(Input& input, const HttpRequest& request, HttpResponse& response) noexcept {
    std::array<char, kJobIdSize + 1> id{};
    AcquireSRWLockExclusive(&g_lock);
    bool queued = false;
    if (g_workerRunning) {
        sweep_locked(now_seconds());
        for (Job& job : g_jobs) {
            if (job.state == JobState::empty) {
                if (new_job_id(id)) {
                    job.state = JobState::queued;
                    job.id = id;
                    job.input = std::move(input);
                    queued = true;
                }
                break;
            }
        }
    }
    const bool running = g_workerRunning;
    ReleaseSRWLockExclusive(&g_lock);
    wipe(input);

    if (!queued) {
        write_outcome(request,
                      response,
                      running ? error(kHttpUnavailable, "busy", "Too many requests right now. Try again in a moment.")
                              : error(kHttpUnavailable, "unavailable", "The server can't reach Discord right now."));
        return;
    }
    WakeConditionVariable(&g_wake);
    const std::string accepted = "{\"job\":\"" + std::string(id.data()) + "\",\"retryAfterMs\":1000}";
    write_json(request, response, kHttpAccepted, accepted);
}

[[nodiscard]] std::string_view body_of(const HttpRequest& request) noexcept {
    return {reinterpret_cast<const char*>(request.body.data()), request.body.size()};
}

[[nodiscard]] bool read_provider(std::string_view body, Input& input) noexcept {
    std::string provider;
    return body.size() <= 8192 && json_string(body, "provider", provider, 16) && provider == "discord"
           && json_string(body, "providerAccessToken", input.providerToken, kMaximumProviderToken)
           && printable_token(input.providerToken, 10, kMaximumProviderToken);
}

void refuse_malformed(Input& input, const HttpRequest& request, HttpResponse& response) noexcept {
    wipe(input);
    write_outcome(request, response, error(kHttpBadRequest, "bad_request", "The request was malformed."));
}

[[nodiscard]] bool load_config(void* module, Config& output) noexcept {
    output = default_config();
    dawn::core::path::Buffer path;
    if (!dawn::core::path::artifact_directory(module, path)
        || !dawn::core::path::append(path, kConfigSuffix)) {
        return false;
    }
    const HANDLE file = CreateFileW(path.chars.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        log_line(dawn::core::log::Level::info, "ev=expansions stage=config result=absent enabled=0");
        return true;
    }
    std::array<char, kConfigCapacity> text{};
    DWORD read = 0;
    const bool readOk = ReadFile(file, text.data(), static_cast<DWORD>(text.size() - 1), &read, nullptr) != FALSE;
    (void)CloseHandle(file);
    const std::string_view json(text.data(), readOk ? read : 0);

    Config parsed = default_config();
    bool enabled = false;
    const bool valid = readOk && json_bool(json, "enabled", enabled)
                       && (value_of(json, "forsaken") == std::string_view::npos || json_names(json, "forsaken", parsed.forsaken))
                       && (value_of(json, "shadowkeep") == std::string_view::npos || json_names(json, "shadowkeep", parsed.shadowkeep));
    if (!valid) {
        // a broken policy must not lock everyone out; leave checks off and say so
        log_line(dawn::core::log::Level::warn, "ev=expansions stage=config result=invalid enabled=0");
        return true;
    }
    parsed.enabled = enabled;
    output = parsed;

    const entitlements::Table& table = entitlements::get();
    for (const NameList* list : {&output.forsaken, &output.shadowkeep}) {
        for (std::size_t index = 0; index < list->count; ++index) {
            bool present = false;
            for (std::size_t row = 0; row < table.count; ++row) {
                present = present || entitlements::name_of(table.entries[row]) == std::string_view(list->names[index].data());
            }
            if (!present) {
                log_line(dawn::core::log::Level::warn,
                         "ev=expansions stage=config result=unknown_row name=%s",
                         list->names[index].data());
            }
        }
    }
    log_line(dawn::core::log::Level::info,
             "ev=expansions stage=config result=ok enabled=%d forsaken_rows=%zu shadowkeep_rows=%zu",
             output.enabled ? 1 : 0,
             output.forsaken.count,
             output.shadowkeep.count);
    return true;
}

void withdraw(entitlements::Table& table, const NameList& rows, bool owned) noexcept {
    if (owned) {
        return;
    }
    for (std::size_t index = 0; index < std::min(table.count, table.entries.size()); ++index) {
        if (in_list(rows, entitlements::name_of(table.entries[index]))) {
            table.entries[index].ownership = entitlements::Ownership::none;
        }
    }
}

} // namespace

bool initialize(void* module) noexcept {
    Config config{};
    if (!load_config(module, config)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_config = config;
    g_stopping = false;
    ReleaseSRWLockExclusive(&g_lock);
    if (!https_client::initialize()) {
        log_line(dawn::core::log::Level::warn, "ev=launcher_api stage=https result=fail");
        return true;
    }
    g_worker = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
    if (g_worker == nullptr) {
        https_client::shutdown();
        log_line(dawn::core::log::Level::warn, "ev=launcher_api stage=worker result=fail");
        return true;
    }
    AcquireSRWLockExclusive(&g_lock);
    g_workerRunning = true;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_stopping = true;
    g_workerRunning = false;
    ReleaseSRWLockExclusive(&g_lock);
    WakeAllConditionVariable(&g_wake);
    if (g_worker != nullptr) {
        (void)WaitForSingleObject(g_worker, kWorkerStopMilliseconds);
        (void)CloseHandle(g_worker);
        g_worker = nullptr;
    }
    AcquireSRWLockExclusive(&g_lock);
    for (Job& job : g_jobs) {
        wipe(job.input);
        job = Job{};
    }
    g_config = Config{};
    ReleaseSRWLockExclusive(&g_lock);
    https_client::shutdown();
}

bool handle_submit(const HttpRequest& request, HttpResponse& response) noexcept {
    AcquireSRWLockShared(&g_lock);
    const bool enabled = g_config.enabled;
    ReleaseSRWLockShared(&g_lock);
    if (!enabled) {
        write_outcome(request, response, error(kHttpUnavailable, "expansion_checks_disabled", "This server isn't checking expansions."));
        return true;
    }

    const std::string_view body = body_of(request);
    Input input{};
    input.kind = JobKind::expansion;
    std::string steamIdText;
    std::string accountName;
    const bool parsed = read_provider(body, input)
                        && json_string(body, "steamAccessToken", input.steamToken, kMaximumSteamToken)
                        && printable_token(input.steamToken, 20, kMaximumSteamToken)
                        && json_string(body, "steamId", steamIdText, 20) && decimal(steamIdText, input.steamId)
                        && (input.steamId & 0xFFFFFFFF00000000ULL) == kSteamIndividualPrefix
                        && static_cast<std::uint32_t>(input.steamId) != 0
                        && json_string(body, "steamAccountName", accountName, account_store::kAccountNameCapacity - 1)
                        && printable_token(accountName, 1, account_store::kAccountNameCapacity - 1);
    if (!parsed) {
        refuse_malformed(input, request, response);
        return true;
    }
    std::copy(steamIdText.begin(), steamIdText.end(), input.steamIdText.begin());
    std::copy(accountName.begin(), accountName.end(), input.accountName.begin());
    queue(input, request, response);
    return true;
}

bool handle_profile(const HttpRequest& request, HttpResponse& response) noexcept {
    Input input{};
    input.kind = JobKind::profile;
    if (!read_provider(body_of(request), input)) {
        refuse_malformed(input, request, response);
        return true;
    }
    queue(input, request, response);
    return true;
}

bool handle_issue_code(const HttpRequest& request, HttpResponse& response) noexcept {
    const std::string_view body = body_of(request);
    Input input{};
    input.kind = JobKind::issueCode;
    NameList names{};
    if (!read_provider(body, input) || !json_names(body, "grants", names)) {
        refuse_malformed(input, request, response);
        return true;
    }
    for (std::size_t index = 0; index < names.count; ++index) {
        const std::uint32_t bit = roles::grant_from_name(names.names[index].data());
        if (bit == 0) {
            refuse_malformed(input, request, response);
            return true;
        }
        input.grants |= bit;
    }
    queue(input, request, response);
    return true;
}

bool handle_redeem_code(const HttpRequest& request, HttpResponse& response) noexcept {
    const std::string_view body = body_of(request);
    Input input{};
    input.kind = JobKind::redeemCode;
    if (!read_provider(body, input) || !json_string(body, "code", input.code, 64)) {
        refuse_malformed(input, request, response);
        return true;
    }
    queue(input, request, response);
    return true;
}

bool handle_forget_access(const HttpRequest& request, HttpResponse& response) noexcept {
    Input input{};
    input.kind = JobKind::forgetAccess;
    if (!read_provider(body_of(request), input)) {
        refuse_malformed(input, request, response);
        return true;
    }
    queue(input, request, response);
    return true;
}

bool handle_result(const HttpRequest& request, HttpResponse& response) noexcept {
    const std::string_view body = body_of(request);
    std::string id;
    if (body.size() > 256 || !json_string(body, "job", id, kJobIdSize) || id.size() != kJobIdSize) {
        write_outcome(request, response, error(kHttpBadRequest, "bad_request", "The request was malformed."));
        return true;
    }

    Outcome result{};
    bool pending = false;
    bool found = false;
    AcquireSRWLockExclusive(&g_lock);
    sweep_locked(now_seconds());
    if (Job* job = find_locked(id); job != nullptr) {
        found = true;
        if (job->state == JobState::done) {
            result.status = job->status;
            result.body = job->body;
            result.bodySize = job->bodySize;
            *job = Job{};
        } else {
            pending = true;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (!found) {
        write_outcome(request, response, error(kHttpNotFound, "job_not_found", "That request expired. Try again."));
        return true;
    }
    if (pending) {
        write_json(request, response, kHttpAccepted, "{\"job\":\"" + id + "\",\"retryAfterMs\":1000}");
        return true;
    }
    write_outcome(request, response, result);
    return true;
}

entitlements::Table signon_table(std::uint64_t platformId) noexcept {
    report_bonus_rewards();
    entitlements::Table table = entitlements::get();
    AcquireSRWLockShared(&g_lock);
    const bool enabled = g_config.enabled;
    const NameList forsakenRows = g_config.forsaken;
    const NameList shadowkeepRows = g_config.shadowkeep;
    ReleaseSRWLockShared(&g_lock);
    if (!enabled) {
        return table;
    }
    const account_store::ExpansionOwnership owned = account_store::expansion_ownership(platformId);
    withdraw(table, forsakenRows, owned.forsaken);
    withdraw(table, shadowkeepRows, owned.shadowkeep);
    log_line(dawn::core::log::Level::info,
             "ev=expansions stage=signon checked=%d forsaken=%d shadowkeep=%d",
             owned.known ? 1 : 0,
             owned.forsaken ? 1 : 0,
             owned.shadowkeep ? 1 : 0);
    return table;
}

} // namespace restoration::expansions
