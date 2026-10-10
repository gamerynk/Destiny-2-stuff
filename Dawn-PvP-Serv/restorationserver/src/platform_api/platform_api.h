#pragma once

#include <cstdint>
#include <string_view>

#include "client/network/consumer.h"

namespace restoration::platform_api {

// Bungie.net Platform surface pinned at Bungie-net/api 3363bc37 (client build 86657).
// Separate from the /v1 routes: this is someone else's contract, so shapes follow the OpenAPI doc exactly

enum class ErrorCode : std::int32_t {
    success = 1,
    systemDisabled = 5,
    parameterParseFailure = 7,
    parameterInvalidRange = 8,
    webAuthRequired = 99,
    userCannotResolveCentralAccount = 217,
    destinyAccountNotFound = 1601,
    destinyUnexpectedError = 1618,
    destinyCharacterNotFound = 1620,
    destinyInvalidMembershipType = 1630,
    apiInvalidOrExpiredKey = 2101,
    apiKeyMissingFromRequest = 2102,
};

enum class MembershipType : std::int32_t {
    none = 0,
    tigerXbox = 1,
    tigerPsn = 2,
    tigerSteam = 3,
    tigerBlizzard = 4,
    tigerStadia = 5,
    tigerDemon = 10,
    bungieNext = 254,
    all = -1,
};

[[nodiscard]] bool consume(const dawn::client::network::HttpRequest& request,
                           dawn::client::network::HttpResponse& response) noexcept;

[[nodiscard]] bool owns_route(std::string_view url) noexcept;

} // namespace restoration::platform_api
