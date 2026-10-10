#include "matchmaking_response_encoder.h"

#include <array>

#include "../../../protobuf/codec.h"
#include "matchmaking_dynamic_response.h"
#include "../social_search.h"

namespace dawn::middleware::bap::matchmaking::response {
namespace {

using protobuf::Writer;

constexpr std::uint32_t kSearchResultsField = 3;
constexpr std::uint32_t kConfigurationField = 4;
constexpr std::uint32_t kLiveStatsField = 8;

constexpr std::uint32_t kServiceConfiguration = 1;
constexpr std::uint32_t kSearchOnlySeconds = 60;
constexpr std::uint32_t kDesperationSeconds = 60;
constexpr std::uint32_t kMaximumPlayers = 9;
constexpr std::uint32_t kMaximumPosse = 3;
constexpr std::uint32_t kMaximumMatchmadePlayers = 6;

[[nodiscard]] bool encode_empty_message(std::uint32_t fieldNumber,
                                        std::span<std::byte> output,
                                        std::size_t& written) noexcept {
    Writer writer(output);
    if (!writer.write_length_delimited(fieldNumber, {})) {
        return false;
    }
    written = writer.size();
    return true;
}

// nanopb: f4 configuration { f1 lane_policy { f1 service_config, f3 composition_wait_s, f6
// search_only_s, f7 desperation_s, f13 provider_policy }, f2 bubble_policies }
[[nodiscard]] bool encode_configuration(std::span<std::byte> output,
                                        std::size_t& written) noexcept {
    // one present zero-valued entry is the smallest valid repeated-message shape; it runs
    // the native provider loop once without guessing the four unnamed uint32 fields
    std::array<std::byte, 8> providerPolicy{};
    Writer providerPolicyWriter(providerPolicy);
    if (!providerPolicyWriter.write_length_delimited(3, {})) {
        return false;
    }

    std::array<std::byte, 48> lanePolicy{};
    Writer laneWriter(lanePolicy);
    if (!laneWriter.write_varint(1, kServiceConfiguration)
        || !laneWriter.write_varint(6, kSearchOnlySeconds)
        || !laneWriter.write_varint(7, kDesperationSeconds)
        || !laneWriter.write_length_delimited(
            13, {providerPolicy.data(), providerPolicyWriter.size()})) {
        return false;
    }

    std::array<std::byte, 32> defaultBubble{};
    Writer defaultBubbleWriter(defaultBubble);
    if (!defaultBubbleWriter.write_varint(1, kMaximumPlayers)
        || !defaultBubbleWriter.write_varint(2, kMaximumPosse)
        || !defaultBubbleWriter.write_varint(3, kMaximumMatchmadePlayers)
        || !defaultBubbleWriter.write_varint(4, kServiceConfiguration)) {
        return false;
    }

    std::array<std::byte, 40> bubblePolicies{};
    Writer bubblePoliciesWriter(bubblePolicies);
    if (!bubblePoliciesWriter.write_length_delimited(
            1, {defaultBubble.data(), defaultBubbleWriter.size()})) {
        return false;
    }

    std::array<std::byte, 96> configuration{};
    Writer configurationWriter(configuration);
    if (!configurationWriter.write_length_delimited(
            1, {lanePolicy.data(), laneWriter.size()})
        || !configurationWriter.write_length_delimited(
            2, {bubblePolicies.data(), bubblePoliciesWriter.size()})) {
        return false;
    }

    Writer outputWriter(output);
    if (!outputWriter.write_length_delimited(
            kConfigurationField,
            {configuration.data(), configurationWriter.size()})) {
        return false;
    }
    written = outputWriter.size();
    return true;
}

} // namespace

bool encode(const Response& response, std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    switch (response.kind) {
    case RequestKind::none:
    case RequestKind::advertisementDelete:
    case RequestKind::rejoinAdvertisementDelete:
        return true;
    case RequestKind::sessionSearch:
        if(!response.descriptor.empty())return social_search::encode(response.descriptor,output,written);
        return encode_empty_message(kSearchResultsField, output, written);
    case RequestKind::advertisementUpdate:
        return encode_advertisement_id(true, response.advertisementId, output, written);
    case RequestKind::configuration:
        return encode_configuration(output, written);
    case RequestKind::rejoinAdvertisementUpdate:
        return encode_advertisement_id(false, response.advertisementId, output, written);
    case RequestKind::locateSession:
        if (response.descriptor.empty()) {
            return true;
        }
        return encode_locate_result(response.advertisementId, response.descriptor, output, written);
    case RequestKind::liveStats:
        return encode_empty_message(kLiveStatsField, output, written);
    }
    return false;
}

} // namespace dawn::middleware::bap::matchmaking::response
