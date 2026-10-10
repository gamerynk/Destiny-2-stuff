#include <array>
#include <string_view>

#include "game.h"
#include "game/retail_log/retail_log_signature_bytes.h"
#include "signature_text.h"

namespace dawn::client::patterns::game {
namespace {

// guarded read of the signature-enable byte: the store, its preceding call, the rip-relative
// compare against zero and the branch that skips verification
constexpr std::string_view kContentManifestGateText =
    "4C 89 B4 24 ? ? ? ? E8 ? ? ? ? 80 3D ? ? ? ? 00 74";
constexpr auto kContentManifestGate =
    signature<signature_length(kContentManifestGateText)>(kContentManifestGateText);

// roster-prefix decoder that applies all 65 bubble authority lanes; the match runs past the
// stack-cookie store because the prologue alone is not unique
constexpr std::string_view kBubbleAuthorityDecoderText =
    "40 55 53 56 41 55 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 "
    "48 89 85 ? ? ? ? 4C 8B E9 48 89 BC 24";
constexpr auto kBubbleAuthorityDecoder =
    signature<signature_length(kBubbleAuthorityDecoderText)>(kBubbleAuthorityDecoderText);

// process build-state getter used by each bubble authority lane
constexpr std::string_view kContentUntrackedGetterText =
    "48 83 EC ? C7 44 24 ? 00 00 00 00 0F B6 05 ? ? ? ? 85 C0 74 ? 0F B6 05 ? ? ? ?";
constexpr auto kContentUntrackedGetter =
    signature<signature_length(kContentUntrackedGetterText)>(kContentUntrackedGetterText);

// 6-slot object resolver whose first instruction names the schema tables
constexpr std::string_view kQueuezObjectResolverText =
    "4C 8B 1D ? ? ? ? 45 33 C9 48 63 C2 45 8B D0 48 05 1A 25 00 00 48 8D 14 40 48 C1 E2 05";
constexpr auto kQueuezObjectResolver =
    signature<signature_length(kQueuezObjectResolverText)>(kQueuezObjectResolverText);

// family-5 subscription path whose second call returns the object store
constexpr std::string_view kQueuezFamily5SubscribeText =
    "40 57 48 83 EC ? 48 8B F9 E8 ? ? ? ? 0F B6 47 50 84 C0 75 ? E8 ? ? ? ?";
constexpr auto kQueuezFamily5Subscribe =
    signature<signature_length(kQueuezFamily5SubscribeText)>(kQueuezFamily5SubscribeText);

// config-init site that loads the bootstrap content-id token; the address comes from the
// wildcarded rip-relative operand, so no token bytes live in Dawn
constexpr std::string_view kContentIdTokenLoadText =
    "0F 10 44 24 ? 41 B8 14 00 00 00 48 8D 15 ? ? ? ? 0F 10 4C 24 ? 48 8D 8C 24 ? ? ? ?";
constexpr auto kContentIdTokenLoad =
    signature<signature_length(kContentIdTokenLoadText)>(kContentIdTokenLoadText);

// item-stat lookup entry used by equipment scoring; runs into the argument moves because
// the prologue alone is not unique once the frame size is wildcarded
constexpr std::string_view kGetItemStatValueText =
    "40 53 56 41 54 41 56 41 57 48 83 EC ? 45 33 E4 41 0F B6 D9 4C 8B 8C 24 ? ? ? ? 4D 8B F0 "
    "44 89 21";
constexpr auto kGetItemStatValue =
    signature<signature_length(kGetItemStatValueText)>(kGetItemStatValueText);

// light-level conversion entry used by equipment scoring
constexpr std::string_view kLightValueToScalarText =
    "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 0F 29 74 24 ? 41 0F B6 D8";
constexpr auto kLightValueToScalar =
    signature<signature_length(kLightValueToScalarText)>(kLightValueToScalarText);

constexpr std::array kDefinitions{
    patterns::Pattern{"content_manifest_gate", kContentManifestGate},
    patterns::Pattern{"bubble_authority_decoder", kBubbleAuthorityDecoder},
    patterns::Pattern{"content_untracked_getter", kContentUntrackedGetter},
    patterns::Pattern{"content_id_token_load", kContentIdTokenLoad},
    patterns::Pattern{"queuez_object_resolver", kQueuezObjectResolver},
    patterns::Pattern{"queuez_family5_subscribe", kQueuezFamily5Subscribe},
    patterns::Pattern{"get_item_stat_value", kGetItemStatValue},
    patterns::Pattern{"light_value_to_scalar", kLightValueToScalar},
    patterns::Pattern{"retail_log_enqueue", retail_log::kEnqueue},
    patterns::Pattern{"retail_log_set_category_verbosity", retail_log::kSetCategoryVerbosity},
};

static_assert(kDefinitions.size() == static_cast<std::size_t>(Id::count));

} // namespace

std::span<const patterns::Pattern> definitions() noexcept {
    return kDefinitions;
}

} // namespace dawn::client::patterns::game
