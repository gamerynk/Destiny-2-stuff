#include "platform_api.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

#include "../account_store.h"
#include "json_writer.h"
#include "core/logging/log.h"
#include "state/account/account_state.h"
#include "state/content_manifest/content_manifest_state_runtime.h"
#include "state/equipment/light/resolution/configured_equipment_light_resolver.h"

namespace restoration::platform_api {
namespace {

namespace inventory = dawn::state::account::inventory;
namespace light = dawn::state::equipment::light;

constexpr std::string_view kMountPoint = "/Platform";

constexpr unsigned kHttpOk = 200;
constexpr unsigned kHttpBadRequest = 400;
constexpr unsigned kHttpUnauthorized = 401;
constexpr unsigned kHttpNotFound = 404;
constexpr unsigned kHttpUnavailable = 503;

constexpr std::string_view kJsonContentType = "application/json; charset=utf-8";

// accounts are Steam identities, so any other concrete type gets DestinyInvalidMembershipType like live
constexpr std::int32_t kServedMembershipType = static_cast<std::int32_t>(MembershipType::tigerSteam);

// class, race and gender hashes are left 0: the manifest isn't read yet and a wrong hash is
// worse than none; the typed classType/raceType/genderType fields are exact
constexpr std::uint32_t kUnresolvedDefinitionHash = 0;

// the client sends absolute-form targets, which never match an exact comparison; reduce to the path
[[nodiscard]] std::string_view path_of(std::string_view url) noexcept {
    const std::size_t scheme = url.find("://");
    if (scheme != std::string_view::npos) {
        const std::size_t authority = url.find('/', scheme + 3);
        url = authority == std::string_view::npos ? std::string_view{} : url.substr(authority);
    }
    const std::size_t query = url.find('?');
    return query == std::string_view::npos ? url : url.substr(0, query);
}

[[nodiscard]] std::string_view query_of(std::string_view url) noexcept {
    const std::size_t query = url.find('?');
    if (query == std::string_view::npos) {
        return {};
    }
    std::string_view value = url.substr(query + 1);
    const std::size_t fragment = value.find('#');
    return fragment == std::string_view::npos ? value : value.substr(0, fragment);
}

[[nodiscard]] char lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] bool equals_ignoring_case(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size()
           && std::equal(left.begin(), left.end(), right.begin(),
                         [](char a, char b) { return lower(a) == lower(b); });
}

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] std::string_view header_value(std::string_view headers,
                                            std::string_view name) noexcept {
    std::size_t lineStart = 0;
    while (lineStart < headers.size()) {
        const std::size_t lineEnd = headers.find("\r\n", lineStart);
        const std::size_t end = lineEnd == std::string_view::npos ? headers.size() : lineEnd;
        const std::string_view line = headers.substr(lineStart, end - lineStart);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos
            && equals_ignoring_case(line.substr(0, colon), name)) {
            return trim(line.substr(colon + 1));
        }
        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 2;
    }
    return {};
}

[[nodiscard]] std::string_view query_value(std::string_view query,
                                           std::string_view name) noexcept {
    std::size_t start = 0;
    while (start <= query.size()) {
        const std::size_t end = query.find('&', start);
        const std::string_view pair =
            query.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                              : end - start);
        const std::size_t equals = pair.find('=');
        if (equals != std::string_view::npos
            && equals_ignoring_case(pair.substr(0, equals), name)) {
            return pair.substr(equals + 1);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return {};
}

// Bungie routes end in a slash, so a trailing empty segment must not change the match
struct Segments final {
    std::array<std::string_view, 12> values{};
    std::size_t count{};

    [[nodiscard]] std::string_view at(std::size_t index) const noexcept {
        return index < count ? values[index] : std::string_view{};
    }
};

[[nodiscard]] Segments split_path(std::string_view path) noexcept {
    Segments output{};
    std::size_t start = 0;
    while (start < path.size() && output.count < output.values.size()) {
        const std::size_t end = path.find('/', start);
        const std::size_t stop = end == std::string_view::npos ? path.size() : end;
        if (stop > start) {
            output.values[output.count++] = path.substr(start, stop - start);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return output;
}

[[nodiscard]] bool parse_unsigned(std::string_view text, std::uint64_t& output) noexcept {
    if (text.empty() || text.size() > 20) {
        return false;
    }
    std::uint64_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    output = value;
    return true;
}

[[nodiscard]] bool parse_signed(std::string_view text, std::int64_t& output) noexcept {
    const bool negative = !text.empty() && text.front() == '-';
    std::uint64_t magnitude = 0;
    if (!parse_unsigned(negative ? text.substr(1) : text, magnitude)
        || magnitude > static_cast<std::uint64_t>(INT64_MAX)) {
        return false;
    }
    output = negative ? -static_cast<std::int64_t>(magnitude)
                      : static_cast<std::int64_t>(magnitude);
    return true;
}

[[nodiscard]] std::string_view error_status(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::success: return "Success";
    case ErrorCode::systemDisabled: return "SystemDisabled";
    case ErrorCode::parameterParseFailure: return "ParameterParseFailure";
    case ErrorCode::parameterInvalidRange: return "ParameterInvalidRange";
    case ErrorCode::webAuthRequired: return "WebAuthRequired";
    case ErrorCode::userCannotResolveCentralAccount: return "UserCannotResolveCentralAccount";
    case ErrorCode::destinyAccountNotFound: return "DestinyAccountNotFound";
    case ErrorCode::destinyUnexpectedError: return "DestinyUnexpectedError";
    case ErrorCode::destinyCharacterNotFound: return "DestinyCharacterNotFound";
    case ErrorCode::destinyInvalidMembershipType: return "DestinyInvalidMembershipType";
    case ErrorCode::apiInvalidOrExpiredKey: return "ApiInvalidOrExpiredKey";
    case ErrorCode::apiKeyMissingFromRequest: return "ApiKeyMissingFromRequest";
    }
    return "DestinyUnexpectedError";
}

[[nodiscard]] std::string_view error_message(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::success: return "Ok";
    case ErrorCode::systemDisabled: return "This system is temporarily disabled.";
    case ErrorCode::parameterParseFailure: return "A parameter could not be parsed.";
    case ErrorCode::parameterInvalidRange: return "A parameter was out of range.";
    case ErrorCode::webAuthRequired: return "Authentication is required.";
    case ErrorCode::userCannotResolveCentralAccount:
        return "That membership could not be resolved.";
    case ErrorCode::destinyAccountNotFound: return "That Destiny account was not found.";
    case ErrorCode::destinyUnexpectedError: return "An unexpected error occurred.";
    case ErrorCode::destinyCharacterNotFound: return "That character was not found.";
    case ErrorCode::destinyInvalidMembershipType:
        return "That membership type is not served here.";
    case ErrorCode::apiInvalidOrExpiredKey: return "The API key is invalid or expired.";
    case ErrorCode::apiKeyMissingFromRequest: return "An API key is required.";
    }
    return "An unexpected error occurred.";
}

void close_envelope(JsonWriter& writer, ErrorCode code) {
    writer.member("ErrorCode", static_cast<std::int64_t>(code));
    writer.member("ThrottleSeconds", static_cast<std::int64_t>(0));
    writer.member("ErrorStatus", error_status(code));
    writer.member("Message", error_message(code));
    writer.key("MessageData");
    writer.begin_object();
    writer.end_object();
    writer.end_object();
}

[[nodiscard]] bool finish(const std::string& body,
                          unsigned status,
                          const dawn::client::network::HttpRequest& request,
                          dawn::client::network::HttpResponse& response) noexcept {
    if (body.size() > request.response.size()) {
        dawn::core::log::write(dawn::core::log::Channel::server,
                               dawn::core::log::Level::warn,
                               "platform: response exceeded the caller's buffer");
        response.statusCode = kHttpUnavailable;
        response.size = 0;
        response.contentType = kJsonContentType;
        return true;
    }
    std::copy(body.begin(), body.end(), reinterpret_cast<char*>(request.response.data()));
    response.statusCode = status;
    response.size = body.size();
    response.contentType = kJsonContentType;
    return true;
}

[[nodiscard]] bool fail(ErrorCode code,
                        unsigned status,
                        const dawn::client::network::HttpRequest& request,
                        dawn::client::network::HttpResponse& response) noexcept {
    std::string body;
    body.reserve(256);
    JsonWriter writer(body);
    writer.begin_object();
    writer.member_null("Response");
    close_envelope(writer, code);
    return finish(body, status, request, response);
}

void write_timestamp(JsonWriter& writer, std::string_view name, std::uint64_t seconds) {
    const auto time = static_cast<std::time_t>(seconds);
    std::tm parts{};
#if defined(_WIN32)
    const bool converted = ::gmtime_s(&parts, &time) == 0;
#else
    const bool converted = ::gmtime_r(&time, &parts) != nullptr;
#endif
    if (!converted) {
        writer.member(name, std::string_view{"1970-01-01T00:00:00Z"});
        return;
    }
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(),
                                      "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                      parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                                      parts.tm_hour, parts.tm_min, parts.tm_sec);
    writer.member(name, written > 0
                            ? std::string_view(buffer.data(), static_cast<std::size_t>(written))
                            : std::string_view{"1970-01-01T00:00:00Z"});
}

[[nodiscard]] std::uint64_t now_seconds() noexcept {
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

void write_user_info_card(JsonWriter& writer,
                          std::uint64_t membershipId,
                          std::string_view displayName) {
    writer.begin_object();
    writer.member("iconPath", std::string_view{});
    writer.member("crossSaveOverride", static_cast<std::int64_t>(0));
    writer.key("applicableMembershipTypes");
    writer.begin_array();
    writer.number(static_cast<std::int64_t>(kServedMembershipType));
    writer.end_array();
    writer.member("isPublic", true);
    writer.member("membershipType", static_cast<std::int64_t>(kServedMembershipType));
    writer.member_id("membershipId", membershipId);
    writer.member("displayName", displayName);
    writer.end_object();
}

void write_item_component(JsonWriter& writer, const inventory::Item& item) {
    writer.begin_object();
    writer.member_hash("itemHash", item.definitionHash);
    writer.member_id("itemInstanceId", item.instanceSoid);
    writer.member("quantity", static_cast<std::int64_t>(item.quantity));
    writer.member("bindStatus", static_cast<std::int64_t>(0));
    writer.member("location", static_cast<std::int64_t>(0));
    writer.member_hash("bucketHash", kUnresolvedDefinitionHash);
    writer.member("transferStatus", static_cast<std::int64_t>(0));
    writer.member("lockable", true);
    // DestinyItemState bit 0 is Locked
    writer.member("state",
                  static_cast<std::int64_t>((item.flags & inventory::kLockedItemFlag) != 0 ? 1 : 0));
    writer.end_object();
}

void write_profile_item_component(JsonWriter& writer, const inventory::ProfileItem& item) {
    writer.begin_object();
    writer.member_hash("itemHash", item.definitionHash);
    writer.member_id("itemInstanceId", item.instanceSoid);
    writer.member("quantity", static_cast<std::int64_t>(item.quantity));
    writer.member("bindStatus", static_cast<std::int64_t>(0));
    writer.member("location", static_cast<std::int64_t>(0));
    writer.member_hash("bucketHash", kUnresolvedDefinitionHash);
    writer.member("transferStatus", static_cast<std::int64_t>(0));
    writer.member("lockable", false);
    writer.member("state", static_cast<std::int64_t>(0));
    writer.end_object();
}

void write_character_component(JsonWriter& writer,
                               const dawn::state::AccountState& account,
                               std::size_t characterIndex,
                               std::uint64_t membershipId,
                               std::uint64_t lastPlayed) {
    const dawn::state::CharacterState& character = account.characters[characterIndex];
    std::int32_t characterLight = 0;
    if (!light::resolution::character_light(account, characterIndex, characterLight)) {
        // an equipped item missing from the installed maps is a content mismatch, not a reason to drop the character
        characterLight = 0;
    }

    writer.begin_object();
    writer.member_id("membershipId", membershipId);
    writer.member("membershipType", static_cast<std::int64_t>(kServedMembershipType));
    writer.member_id("characterId", character.soid);
    write_timestamp(writer, "dateLastPlayed", lastPlayed);
    writer.member("minutesPlayedThisSession", std::string_view{"0"});
    writer.member("minutesPlayedTotal", std::string_view{"0"});
    writer.member("light", static_cast<std::int64_t>(characterLight));
    writer.key("stats");
    writer.begin_object();
    writer.end_object();
    writer.member_hash("raceHash", kUnresolvedDefinitionHash);
    writer.member_hash("genderHash", kUnresolvedDefinitionHash);
    writer.member_hash("classHash", kUnresolvedDefinitionHash);
    writer.member("raceType", static_cast<std::int64_t>(character.race));
    writer.member("classType", static_cast<std::int64_t>(character.characterClass));
    writer.member("genderType", static_cast<std::int64_t>(character.gender));
    writer.member("emblemPath", std::string_view{});
    writer.member("emblemBackgroundPath", std::string_view{});
    writer.member_hash("emblemHash", kUnresolvedDefinitionHash);
    writer.key("levelProgression");
    writer.begin_object();
    writer.member_hash("progressionHash", kUnresolvedDefinitionHash);
    writer.member("level", static_cast<std::int64_t>(character.level));
    writer.member("levelCap", static_cast<std::int64_t>(50));
    writer.member("progressToNextLevel", static_cast<std::int64_t>(0));
    writer.member("nextLevelAt", static_cast<std::int64_t>(0));
    writer.end_object();
    writer.member("baseCharacterLevel", static_cast<std::int64_t>(character.level));
    writer.member("percentToNextLevel", 0.0);
    writer.end_object();
}

// privacy 1 is Public
void begin_component(JsonWriter& writer, std::string_view name) {
    writer.key(name);
    writer.begin_object();
    writer.key("data");
}

void end_component(JsonWriter& writer) {
    writer.member("privacy", static_cast<std::int64_t>(1));
    writer.end_object();
}

struct ComponentSet final {
    bool profiles{};
    bool profileInventories{};
    bool characters{};
    bool characterInventories{};
    bool characterEquipment{};

    [[nodiscard]] bool any() const noexcept {
        return profiles || profileInventories || characters || characterInventories
               || characterEquipment;
    }
};

void apply_component(ComponentSet& set, std::string_view token) noexcept {
    token = trim(token);
    if (token == "100" || equals_ignoring_case(token, "Profiles")) {
        set.profiles = true;
    } else if (token == "102" || equals_ignoring_case(token, "ProfileInventories")) {
        set.profileInventories = true;
    } else if (token == "200" || equals_ignoring_case(token, "Characters")) {
        set.characters = true;
    } else if (token == "201" || equals_ignoring_case(token, "CharacterInventories")) {
        set.characterInventories = true;
    } else if (token == "205" || equals_ignoring_case(token, "CharacterEquipment")) {
        set.characterEquipment = true;
    }
    // unknown components are ignored, not refused, as the live service does
}

[[nodiscard]] ComponentSet parse_components(std::string_view query) noexcept {
    const std::string_view value = query_value(query, "components");
    ComponentSet set{};
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(',', start);
        apply_component(set, value.substr(start, end == std::string_view::npos
                                                     ? std::string_view::npos
                                                     : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return set;
}

struct ResolvedAccount final {
    dawn::state::AccountState state{};
    std::array<char, account_store::kAccountNameCapacity> displayName{};
    std::uint64_t revision{};
};

[[nodiscard]] bool resolve_account(std::uint64_t membershipId, ResolvedAccount& output) noexcept {
    return account_store::profile_by_platform(membershipId, output.state, output.revision,
                                              output.displayName);
}

[[nodiscard]] std::string_view name_view(
    const std::array<char, account_store::kAccountNameCapacity>& name) noexcept {
    const std::size_t length = static_cast<std::size_t>(
        std::find(name.begin(), name.end(), '\0') - name.begin());
    return std::string_view(name.data(), length);
}

[[nodiscard]] bool find_character(const dawn::state::AccountState& account,
                                  std::uint64_t characterId,
                                  std::size_t& index) noexcept {
    for (std::size_t row = 0; row < account.characterCount; ++row) {
        if (account.characters[row].soid == characterId) {
            index = row;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool membership_type_served(std::int64_t value) noexcept {
    return value == kServedMembershipType
           || value == static_cast<std::int64_t>(MembershipType::all);
}

[[nodiscard]] bool handle_settings(const dawn::client::network::HttpRequest& request,
                                   dawn::client::network::HttpResponse& response) noexcept {
    std::string body;
    body.reserve(512);
    JsonWriter writer(body);
    writer.begin_object();
    writer.key("Response");
    writer.begin_object();
    writer.key("environments");
    writer.begin_array();
    writer.end_array();
    writer.key("systems");
    writer.begin_object();
    for (const std::string_view system : {std::string_view{"Destiny2"},
                                          std::string_view{"D2Profiles"}}) {
        writer.key(system);
        writer.begin_object();
        writer.member("enabled", true);
        writer.key("parameters");
        writer.begin_object();
        writer.end_object();
        writer.end_object();
    }
    writer.end_object();
    writer.end_object();
    close_envelope(writer, ErrorCode::success);
    return finish(body, kHttpOk, request, response);
}

[[nodiscard]] bool handle_global_alerts(const dawn::client::network::HttpRequest& request,
                                        dawn::client::network::HttpResponse& response) noexcept {
    std::string body;
    body.reserve(256);
    JsonWriter writer(body);
    writer.begin_object();
    writer.key("Response");
    writer.begin_array();
    writer.end_array();
    close_envelope(writer, ErrorCode::success);
    return finish(body, kHttpOk, request, response);
}

[[nodiscard]] bool handle_available_locales(
    const dawn::client::network::HttpRequest& request,
    dawn::client::network::HttpResponse& response) noexcept {
    std::string body;
    body.reserve(256);
    JsonWriter writer(body);
    writer.begin_object();
    writer.key("Response");
    writer.begin_object();
    writer.member("en", std::string_view{"en"});
    writer.end_object();
    close_envelope(writer, ErrorCode::success);
    return finish(body, kHttpOk, request, response);
}

struct ManifestVersion final {
    std::array<char, 64> value{};
    std::size_t size{};
};

[[nodiscard]] bool copy_manifest_version(
    void* value, const dawn::state::content_manifest::View& view) noexcept {
    auto& context = *static_cast<ManifestVersion*>(value);
    if (view.guid.size() > context.value.size()) {
        return false;
    }
    std::copy(view.guid.begin(), view.guid.end(), context.value.begin());
    context.size = view.guid.size();
    return true;
}

[[nodiscard]] bool handle_manifest(const dawn::client::network::HttpRequest& request,
                                   dawn::client::network::HttpResponse& response) noexcept {
    ManifestVersion version{};
    if (!dawn::state::content_manifest::visit_snapshot(copy_manifest_version, &version)) {
        return fail(ErrorCode::systemDisabled, kHttpUnavailable, request, response);
    }

    std::string body;
    body.reserve(512);
    JsonWriter writer(body);
    writer.begin_object();
    writer.key("Response");
    writer.begin_object();
    writer.member("version", std::string_view(version.value.data(), version.size));
    writer.member("mobileAssetContentPath", std::string_view{});
    writer.key("mobileGearAssetDataBases");
    writer.begin_array();
    writer.end_array();
    writer.key("mobileWorldContentPaths");
    writer.begin_object();
    writer.end_object();
    writer.key("jsonWorldContentPaths");
    writer.begin_object();
    writer.end_object();
    writer.key("jsonWorldComponentContentPaths");
    writer.begin_object();
    writer.end_object();
    writer.member("mobileClanBannerDatabasePath", std::string_view{});
    writer.key("mobileGearCDN");
    writer.begin_object();
    writer.end_object();
    writer.key("iconImagePyramidInfo");
    writer.begin_array();
    writer.end_array();
    writer.end_object();
    close_envelope(writer, ErrorCode::success);
    return finish(body, kHttpOk, request, response);
}

[[nodiscard]] bool handle_linked_profiles(std::uint64_t membershipId,
                                          const dawn::client::network::HttpRequest& request,
                                          dawn::client::network::HttpResponse& response) noexcept {
    ResolvedAccount account{};
    if (!resolve_account(membershipId, account)) {
        return fail(ErrorCode::destinyAccountNotFound, kHttpNotFound, request, response);
    }

    std::string body;
    body.reserve(1024);
    JsonWriter writer(body);
    writer.begin_object();
    writer.key("Response");
    writer.begin_object();
    writer.key("profiles");
    writer.begin_array();
    writer.begin_object();
    writer.member("iconPath", std::string_view{});
    writer.member("crossSaveOverride", static_cast<std::int64_t>(0));
    writer.key("applicableMembershipTypes");
    writer.begin_array();
    writer.number(static_cast<std::int64_t>(kServedMembershipType));
    writer.end_array();
    writer.member("isPublic", true);
    writer.member("membershipType", static_cast<std::int64_t>(kServedMembershipType));
    writer.member_id("membershipId", membershipId);
    writer.member("displayName", name_view(account.displayName));
    write_timestamp(writer, "dateLastPlayed", now_seconds());
    writer.member("isOverridden", false);
    writer.member("isCrossSavePrimary", false);
    writer.key("unpairedGameVersions");
    writer.begin_array();
    writer.end_array();
    writer.end_object();
    writer.end_array();
    writer.key("profilesWithErrors");
    writer.begin_array();
    writer.end_array();
    writer.end_object();
    close_envelope(writer, ErrorCode::success);
    return finish(body, kHttpOk, request, response);
}

[[nodiscard]] bool handle_profile(std::uint64_t membershipId,
                                  std::string_view query,
                                  const dawn::client::network::HttpRequest& request,
                                  dawn::client::network::HttpResponse& response) noexcept {
    const ComponentSet components = parse_components(query);
    if (!components.any()) {
        return fail(ErrorCode::parameterParseFailure, kHttpBadRequest, request, response);
    }

    ResolvedAccount account{};
    if (!resolve_account(membershipId, account)) {
        return fail(ErrorCode::destinyAccountNotFound, kHttpNotFound, request, response);
    }
    const dawn::state::AccountState& state = account.state;
    const std::uint64_t lastPlayed = now_seconds();

    std::string body;
    body.reserve(64 * 1024);
    JsonWriter writer(body);
    writer.begin_object();
    writer.key("Response");
    writer.begin_object();

    if (components.profiles) {
        begin_component(writer, "profile");
        writer.begin_object();
        writer.key("userInfo");
        write_user_info_card(writer, membershipId, name_view(account.displayName));
        write_timestamp(writer, "dateLastPlayed", lastPlayed);
        writer.member("versionsOwned", static_cast<std::int64_t>(0));
        writer.key("characterIds");
        writer.begin_array();
        for (std::size_t row = 0; row < state.characterCount; ++row) {
            writer.id(state.characters[row].soid);
        }
        writer.end_array();
        writer.key("seasonHashes");
        writer.begin_array();
        writer.end_array();
        writer.end_object();
        end_component(writer);
    }

    if (components.profileInventories) {
        begin_component(writer, "profileInventory");
        writer.begin_object();
        writer.key("items");
        writer.begin_array();
        for (std::size_t row = 0; row < state.vault.count; ++row) {
            write_item_component(writer, state.vault.values[row]);
        }
        for (std::size_t row = 0; row < state.profileItemCount; ++row) {
            write_profile_item_component(writer, state.profileItems[row]);
        }
        writer.end_array();
        writer.end_object();
        end_component(writer);
    }

    if (components.characters) {
        begin_component(writer, "characters");
        writer.begin_object();
        for (std::size_t row = 0; row < state.characterCount; ++row) {
            std::array<char, 24> key{};
            const int written = std::snprintf(
                key.data(), key.size(), "%llu",
                static_cast<unsigned long long>(state.characters[row].soid));
            if (written <= 0) {
                continue;
            }
            writer.key(std::string_view(key.data(), static_cast<std::size_t>(written)));
            write_character_component(writer, state, row, membershipId, lastPlayed);
        }
        writer.end_object();
        end_component(writer);
    }

    if (components.characterInventories) {
        begin_component(writer, "characterInventories");
        writer.begin_object();
        for (std::size_t row = 0; row < state.characterCount; ++row) {
            std::array<char, 24> key{};
            const int written = std::snprintf(
                key.data(), key.size(), "%llu",
                static_cast<unsigned long long>(state.characters[row].soid));
            if (written <= 0) {
                continue;
            }
            writer.key(std::string_view(key.data(), static_cast<std::size_t>(written)));
            writer.begin_object();
            writer.key("items");
            writer.begin_array();
            const inventory::CharacterItems& items = state.characters[row].inventory;
            for (std::size_t slot = 0; slot < items.count; ++slot) {
                write_item_component(writer, items.values[slot]);
            }
            writer.end_array();
            writer.end_object();
        }
        writer.end_object();
        end_component(writer);
    }

    if (components.characterEquipment) {
        begin_component(writer, "characterEquipment");
        writer.begin_object();
        for (std::size_t row = 0; row < state.characterCount; ++row) {
            std::array<char, 24> key{};
            const int written = std::snprintf(
                key.data(), key.size(), "%llu",
                static_cast<unsigned long long>(state.characters[row].soid));
            if (written <= 0) {
                continue;
            }
            writer.key(std::string_view(key.data(), static_cast<std::size_t>(written)));
            writer.begin_object();
            writer.key("items");
            writer.begin_array();
            for (const std::optional<inventory::Item>& item :
                 state.characters[row].equipment.slots) {
                if (item.has_value()) {
                    write_item_component(writer, *item);
                }
            }
            writer.end_array();
            writer.end_object();
        }
        writer.end_object();
        end_component(writer);
    }

    writer.end_object();
    close_envelope(writer, ErrorCode::success);
    return finish(body, kHttpOk, request, response);
}

[[nodiscard]] bool handle_character(std::uint64_t membershipId,
                                    std::uint64_t characterId,
                                    std::string_view query,
                                    const dawn::client::network::HttpRequest& request,
                                    dawn::client::network::HttpResponse& response) noexcept {
    const ComponentSet components = parse_components(query);
    if (!components.any()) {
        return fail(ErrorCode::parameterParseFailure, kHttpBadRequest, request, response);
    }

    ResolvedAccount account{};
    if (!resolve_account(membershipId, account)) {
        return fail(ErrorCode::destinyAccountNotFound, kHttpNotFound, request, response);
    }
    std::size_t index = 0;
    if (!find_character(account.state, characterId, index)) {
        return fail(ErrorCode::destinyCharacterNotFound, kHttpNotFound, request, response);
    }
    const dawn::state::AccountState& state = account.state;
    const std::uint64_t lastPlayed = now_seconds();

    std::string body;
    body.reserve(32 * 1024);
    JsonWriter writer(body);
    writer.begin_object();
    writer.key("Response");
    writer.begin_object();

    if (components.characters) {
        begin_component(writer, "character");
        write_character_component(writer, state, index, membershipId, lastPlayed);
        end_component(writer);
    }

    if (components.characterInventories) {
        begin_component(writer, "inventory");
        writer.begin_object();
        writer.key("items");
        writer.begin_array();
        const inventory::CharacterItems& items = state.characters[index].inventory;
        for (std::size_t slot = 0; slot < items.count; ++slot) {
            write_item_component(writer, items.values[slot]);
        }
        writer.end_array();
        writer.end_object();
        end_component(writer);
    }

    if (components.characterEquipment) {
        begin_component(writer, "equipment");
        writer.begin_object();
        writer.key("items");
        writer.begin_array();
        for (const std::optional<inventory::Item>& item : state.characters[index].equipment.slots) {
            if (item.has_value()) {
                write_item_component(writer, *item);
            }
        }
        writer.end_array();
        writer.end_object();
        end_component(writer);
    }

    writer.end_object();
    close_envelope(writer, ErrorCode::success);
    return finish(body, kHttpOk, request, response);
}

// no key registry yet, so only presence of X-API-Key is checked
[[nodiscard]] bool api_key_present(const dawn::client::network::HttpRequest& request) noexcept {
    return !header_value(request.headers, "X-API-Key").empty();
}

} // namespace

bool owns_route(std::string_view url) noexcept {
    const std::string_view path = path_of(url);
    return path == kMountPoint
           || (path.size() > kMountPoint.size() && path.starts_with(kMountPoint)
               && path[kMountPoint.size()] == '/');
}

bool consume(const dawn::client::network::HttpRequest& request,
             dawn::client::network::HttpResponse& response) noexcept {
    if (!owns_route(request.url)) {
        return false;
    }
    if (!api_key_present(request)) {
        return fail(ErrorCode::apiKeyMissingFromRequest, kHttpUnauthorized, request, response);
    }

    const std::string_view path = path_of(request.url);
    const std::string_view query = query_of(request.url);
    const Segments segments = split_path(path.substr(kMountPoint.size()));

    if (request.method != dawn::client::network::HttpMethod::get) {
        return fail(ErrorCode::destinyUnexpectedError, kHttpNotFound, request, response);
    }

    const std::string_view head = segments.at(0);

    if (head == "Settings" && segments.count == 1) {
        return handle_settings(request, response);
    }
    if (head == "GlobalAlerts" && segments.count == 1) {
        return handle_global_alerts(request, response);
    }
    if (head == "GetAvailableLocales" && segments.count == 1) {
        return handle_available_locales(request, response);
    }
    if (head != "Destiny2") {
        return fail(ErrorCode::destinyUnexpectedError, kHttpNotFound, request, response);
    }

    if (segments.at(1) == "Manifest" && segments.count == 2) {
        return handle_manifest(request, response);
    }

    std::int64_t membershipType = 0;
    std::uint64_t membershipId = 0;
    if (!parse_signed(segments.at(1), membershipType)
        || segments.at(2) != "Profile"
        || !parse_unsigned(segments.at(3), membershipId)) {
        return fail(ErrorCode::destinyUnexpectedError, kHttpNotFound, request, response);
    }
    if (!membership_type_served(membershipType)) {
        return fail(ErrorCode::destinyInvalidMembershipType, kHttpBadRequest, request, response);
    }

    if (segments.count == 4) {
        return handle_profile(membershipId, query, request, response);
    }
    if (segments.count == 5 && segments.at(4) == "LinkedProfiles") {
        return handle_linked_profiles(membershipId, request, response);
    }
    if (segments.count == 6 && segments.at(4) == "Character") {
        std::uint64_t characterId = 0;
        if (!parse_unsigned(segments.at(5), characterId)) {
            return fail(ErrorCode::parameterParseFailure, kHttpBadRequest, request, response);
        }
        return handle_character(membershipId, characterId, query, request, response);
    }

    return fail(ErrorCode::destinyUnexpectedError, kHttpNotFound, request, response);
}

} // namespace restoration::platform_api
