#include "account_encoder.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "../../../../state/build_data/runtime.h"
#include "../../../../state/unlocks/unlocks_runtime.h"
#include "../progression/progression_bank_keys.h"
#include "layout.h"
#include "preferences/preferences_encoder.h"

namespace dawn::middleware::datagen::family4::account {
namespace {

constexpr std::uint16_t kEmptyDefinitionIndex = (std::numeric_limits<std::uint16_t>::max)();
// signed 32-bit max keeps publicity deadlines beyond a normal session clock
constexpr std::uint64_t kSuppressedPublicityDeadline =
    static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)());
constexpr std::byte kSeenMessageByte{0xFF};
constexpr std::size_t kBucketIdentityCapacity = 256;

// slot isn't authored: the bucket descriptor names the first slot of its run and items
// sharing a bucket take consecutive slots in configuration order
[[nodiscard]] bool place_profile_item(const state::account::inventory::ProfileItem& item,
                                      std::array<std::uint16_t, kBucketIdentityCapacity>& taken,
                                      std::span<inventory::layout::Entry> rows) noexcept {
    // dense item table already carries the bucket; only equipped items and plugs have a detail
    state::build_data::items::Definition definition{};
    state::build_data::items::details::Definition detail{};
    state::build_data::inventory::buckets::Descriptor bucket{};
    if (item.quantity <= 0 || item.mutationSerial < 0
        || !state::build_data::find_item_definition_hash(item.definitionHash, definition)
        || !state::build_data::find_configured_item_detail(definition.definitionIndex, detail)
        || detail.definitionIndex != definition.definitionIndex
        || detail.definitionHash != definition.definitionHash
        || detail.bucketId != definition.bucketId
        || detail.instancedDefinitionState
               != state::build_data::items::details::InstancedDefinitionState::stackable
        || !state::build_data::find_inventory_bucket_descriptor(definition.bucketId, bucket)
        || bucket.arraySelector != state::build_data::inventory::buckets::ArraySelector::profile) {
        return false;
    }
    const bool actionSource = state::build_data::is_profile_action_source(
        definition.definitionIndex, definition.bucketId);
    if (actionSource != (item.instanceSoid != 0)) {
        return false;
    }
    const std::uint16_t used = taken[definition.bucketId];
    if (used >= bucket.slotCount) {
        return false;
    }
    const std::size_t slot = static_cast<std::size_t>(bucket.firstSlot) + used;
    if (slot >= rows.size() || rows[slot].definitionIndex != kEmptyDefinitionIndex) {
        return false;
    }
    taken[definition.bucketId] = static_cast<std::uint16_t>(used + 1);
    rows[slot].definitionIndex = definition.definitionIndex;
    rows[slot].instanceSoid = item.instanceSoid;
    rows[slot].quantity = item.quantity;
    rows[slot].mutationSerial = item.mutationSerial;
    return true;
}

} // namespace

bool encode(const state::AccountState& state, std::span<std::byte> output) noexcept {
    if (state.primarySoid == 0 || !state::account::valid(state)
        || output.size() < layout::kMinimumSize) {
        return false;
    }

    layout::Object object{};
    object.accountSoid = state.primarySoid;
    object.selectedCharacterSoid = state::account::selected_character_soid(state);
    // keep the native one-time setup gate separate from input migration/seed versions
    static_assert(layout::kProfileSetupCompletedOffset >= offsetof(layout::Object, seenPreferencesPadding));
    static_assert(layout::kProfileSetupCompletedOffset < layout::kPreferencesOffset);
    object.seenPreferencesPadding[layout::kProfileSetupCompletedOffset
        - offsetof(layout::Object, seenPreferencesPadding)] =
        state.settings.profileSetupCompleted ? std::byte{1} : std::byte{0};
    if (!roster::initialize(state, object.roster)
        || !preferences::encode(state.settings, object.preferences, object.bindings)) {
        return false;
    }

    const state::unlocks::Table& unlocks = state::unlocks::get();
    object.acquiredFlags = unlocks.accountFlags;
    object.profileUnlockFlags = unlocks.profileFlags;
    object.objectiveValues = unlocks.objectiveValues;
    for (layout::CharacterUnlockBlock& block : object.characterUnlocks) {
        block.flags = unlocks.characterFlags;
    }
    object.publicityExpiries.fill(kSuppressedPublicityDeadline);
    object.seenMessages.fill(kSeenMessageByte);
    for (inventory::layout::Entry& item : object.profileItems) {
        item.definitionIndex = kEmptyDefinitionIndex;
    }
    for (inventory::layout::Entry& item : object.secondaryItems) {
        item.definitionIndex = kEmptyDefinitionIndex;
    }
    if (!progression::key_bank(state::build_data::progressions::Scope::account,
                               object.progressions)) {
        return false;
    }
    std::array<std::uint16_t, kBucketIdentityCapacity> takenSlots{};
    for (std::size_t index = 0; index < state.profileItemCount; ++index) {
        if (!place_profile_item(state.profileItems[index], takenSlots, object.profileItems)) {
            return false;
        }
    }
    // an empty vault needs no descriptor, so a build whose data lacks one still encodes accounts
    // that never used the vault
    if (state.vault.count != 0) {
        state::build_data::inventory::buckets::Descriptor vaultBucket{};
        if (!state::build_data::find_inventory_bucket_descriptor(
                state::account::inventory::kVaultBucketId, vaultBucket)
            || vaultBucket.arraySelector
                   != state::build_data::inventory::buckets::ArraySelector::profile
            || state.vault.count > vaultBucket.slotCount) {
            return false;
        }
        for (std::size_t index = 0; index < state.vault.count; ++index) {
            const state::account::inventory::Item& item = state.vault.values[index];
            state::build_data::items::Definition definition{};
            const std::size_t slot = static_cast<std::size_t>(vaultBucket.firstSlot) + index;
            if (!state::build_data::find_item_definition_hash(item.definitionHash, definition)
                || slot >= object.profileItems.size()
                || object.profileItems[slot].definitionIndex != kEmptyDefinitionIndex) {
                return false;
            }
            object.profileItems[slot].definitionIndex = definition.definitionIndex;
            object.profileItems[slot].instanceSoid = item.instanceSoid;
            object.profileItems[slot].quantity = item.quantity;
            object.profileItems[slot].mutationSerial = item.mutationSerial;
            object.profileItems[slot].flags = item.flags;
        }
    }
    object.profileItemCount =
        static_cast<std::uint32_t>(state.profileItemCount + state.vault.count);

    // commit only after every fallible conversion succeeds so callers never get a partial object
    std::fill(output.begin(), output.end(), std::byte{});
    std::memcpy(output.data(), &object, sizeof object);
    return true;
}

} // namespace dawn::middleware::datagen::family4::account
