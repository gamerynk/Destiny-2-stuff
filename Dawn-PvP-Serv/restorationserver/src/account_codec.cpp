#include "account_codec.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

namespace restoration::account_codec {
namespace {

using dawn::state::AccountState;
namespace inventory = dawn::state::account::inventory;
namespace settings = dawn::state::account::settings;

class Writer final {
public:
    explicit Writer(std::vector<std::byte>& output) noexcept : output_(output) {
        output_.clear();
    }

    [[nodiscard]] bool byte(std::uint8_t value) noexcept {
        try {
            output_.push_back(static_cast<std::byte>(value));
            return true;
        } catch (...) {
            return false;
        }
    }

    template <typename Value>
    [[nodiscard]] bool integer(Value value) noexcept {
        static_assert(std::is_integral_v<Value>);
        using Unsigned = std::make_unsigned_t<Value>;
        Unsigned bits = static_cast<Unsigned>(value);
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            if (!byte(static_cast<std::uint8_t>(bits & static_cast<Unsigned>(0xFFU)))) {
                return false;
            }
            bits >>= 8U;
        }
        return true;
    }

    [[nodiscard]] bool boolean(bool value) noexcept {
        return byte(value ? 1U : 0U);
    }

    [[nodiscard]] bool real(float value) noexcept {
        return integer(std::bit_cast<std::uint32_t>(value));
    }

private:
    std::vector<std::byte>& output_;
};

class Reader final {
public:
    explicit Reader(std::span<const std::byte> input) noexcept : input_(input) {}

    template <typename Value>
    [[nodiscard]] bool integer(Value& value) noexcept {
        static_assert(std::is_integral_v<Value>);
        if (offset_ > input_.size() || input_.size() - offset_ < sizeof(Value)) {
            return false;
        }
        using Unsigned = std::make_unsigned_t<Value>;
        Unsigned bits = 0;
        for (std::size_t index = 0; index < sizeof(Value); ++index) {
            bits |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(input_[offset_++]))
                    << static_cast<unsigned>(index * 8U);
        }
        value = static_cast<Value>(bits);
        return true;
    }

    [[nodiscard]] bool boolean(bool& value) noexcept {
        std::uint8_t raw = 0;
        if (!integer(raw) || raw > 1U) {
            return false;
        }
        value = raw != 0;
        return true;
    }

    [[nodiscard]] bool real(float& value) noexcept {
        std::uint32_t raw = 0;
        if (!integer(raw)) {
            return false;
        }
        value = std::bit_cast<float>(raw);
        return true;
    }

    [[nodiscard]] bool finished() const noexcept {
        return offset_ == input_.size();
    }

private:
    std::span<const std::byte> input_;
    std::size_t offset_{};
};

[[nodiscard]] bool write_optional_u16(Writer& writer,
                                      const std::optional<std::uint16_t>& value) noexcept {
    return writer.boolean(value.has_value())
           && (!value.has_value() || writer.integer(*value));
}

[[nodiscard]] bool read_optional_u16(Reader& reader,
                                     std::optional<std::uint16_t>& value) noexcept {
    bool present = false;
    std::uint16_t decoded = 0;
    if (!reader.boolean(present) || (present && !reader.integer(decoded))) {
        return false;
    }
    value = present ? std::optional<std::uint16_t>(decoded) : std::nullopt;
    return true;
}

[[nodiscard]] bool write_sockets(Writer& writer, const inventory::Sockets& sockets) noexcept {
    if (!writer.byte(static_cast<std::uint8_t>(sockets.policy))
        || !writer.integer(static_cast<std::uint32_t>(sockets.plugCount))) {
        return false;
    }
    for (const auto& plug : sockets.plugs) {
        if (!writer.boolean(plug.has_value())
            || (plug.has_value() && !writer.integer(*plug))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_sockets(Reader& reader, inventory::Sockets& sockets) noexcept {
    std::uint8_t policy = 0;
    std::uint32_t count = 0;
    if (!reader.integer(policy)
        || policy > static_cast<std::uint8_t>(inventory::SocketPolicy::authored)
        || !reader.integer(count) || count > sockets.plugs.size()) {
        return false;
    }
    sockets.policy = static_cast<inventory::SocketPolicy>(policy);
    sockets.plugCount = count;
    for (auto& plug : sockets.plugs) {
        bool present = false;
        std::uint32_t value = 0;
        if (!reader.boolean(present) || (present && !reader.integer(value))) {
            return false;
        }
        plug = present ? std::optional<std::uint32_t>(value) : std::nullopt;
    }
    return true;
}

[[nodiscard]] bool write_item(Writer& writer, const inventory::Item& item) noexcept {
    return writer.integer(item.instanceSoid) && writer.integer(item.definitionHash)
           && writer.integer(item.level) && writer.integer(item.quantity)
           && writer.integer(item.mutationSerial) && writer.integer(item.flags)
           && write_sockets(writer, item.sockets);
}

[[nodiscard]] bool read_item(Reader& reader, inventory::Item& item) noexcept {
    return reader.integer(item.instanceSoid) && reader.integer(item.definitionHash)
           && reader.integer(item.level) && reader.integer(item.quantity)
           && reader.integer(item.mutationSerial) && reader.integer(item.flags)
           && read_sockets(reader, item.sockets);
}

[[nodiscard]] bool write_settings(Writer& writer,
                                  const settings::AccountSettings& value) noexcept {
    const auto& controls = value.controls;
    if (!writer.integer(controls.buttonLayout) || !writer.integer(controls.movementMode)
        || !writer.integer(controls.controllerLookSensitivity)
        || !writer.boolean(controls.controllerInvertVertical)
        || !writer.boolean(controls.controllerAutoLookCentering)
        || !writer.boolean(controls.controllerVibration)
        || !writer.boolean(controls.controllerSwapShoulders)
        || !writer.boolean(controls.controllerInvertHorizontal)
        || !writer.integer(controls.mouseLookSensitivity)
        || !writer.boolean(controls.mouseInvertVertical)
        || !writer.boolean(controls.mouseInvertHorizontal)
        || !writer.boolean(controls.unidentifiedToggle)
        || !writer.boolean(controls.mouseAimSmoothing)
        || !writer.real(controls.adsSensitivityModifier)
        || !writer.integer(controls.doublePressDelay)) {
        return false;
    }
    const auto& audio = value.audio;
    if (!writer.integer(audio.voiceOutputMode) || !writer.integer(audio.teamVoiceChannel)
        || !writer.integer(audio.reservedMode) || !writer.integer(audio.migrationVersion)
        || !writer.integer(audio.chatVolume) || !writer.boolean(audio.muteWhenUnfocused)
        || !writer.integer(audio.soundEffectsVolume) || !writer.integer(audio.dialogueVolume)
        || !writer.integer(audio.musicVolume)) {
        return false;
    }
    const auto& display = value.display;
    if (!writer.integer(display.brightness) || !writer.boolean(display.showFps)
        || !writer.integer(display.hdrMode) || !writer.real(display.calibrationPrimary)
        || !writer.real(display.calibrationAlpha)) {
        return false;
    }
    const auto& interfaceState = value.interface;
    if (!writer.integer(interfaceState.subtitlesMode)
        || !writer.integer(interfaceState.colorblindMode)
        || !writer.integer(interfaceState.helmetMode)
        || !writer.integer(interfaceState.hudOpacity)
        || !writer.boolean(interfaceState.displayHints)
        || !writer.integer(interfaceState.backgroundOpacity)
        || !writer.integer(interfaceState.reticleLocation)
        || !writer.integer(interfaceState.reticleColor)
        || !writer.integer(interfaceState.textSize)
        || !writer.integer(interfaceState.textColor)
        || !writer.integer(interfaceState.textBackgroundStyle)
        || !writer.integer(interfaceState.textBackgroundOpacity)
        || !writer.integer(interfaceState.reservedTextMode)
        || !writer.integer(interfaceState.subtitleOptionsEntry)) {
        return false;
    }
    const auto& social = value.social;
    if (!writer.boolean(social.preferGoodConnection) || !writer.integer(social.textChatMode)
        || !writer.boolean(social.showRealNames)
        || !writer.boolean(social.clanInviteNotifications)
        || !writer.boolean(social.profanityFilter)
        || !writer.boolean(social.voiceChatEnabled)
        || !writer.integer(social.whisperChatMode)
        || !writer.integer(social.teamChatJoinMode)
        || !writer.integer(social.localChatJoinMode)
        || !writer.integer(social.clanChatJoinMode)
        || !writer.integer(social.chatAutoHideMode)
        || !writer.boolean(value.keyBindings.configured)) {
        return false;
    }
    for (const auto& binding : value.keyBindings.values) {
        if (!write_optional_u16(writer, binding.primary)
            || !write_optional_u16(writer, binding.secondary)) {
            return false;
        }
    }
    return writer.boolean(value.configured);
}

[[nodiscard]] bool read_settings(Reader& reader, settings::AccountSettings& value) noexcept {
    auto& controls = value.controls;
    if (!reader.integer(controls.buttonLayout) || !reader.integer(controls.movementMode)
        || !reader.integer(controls.controllerLookSensitivity)
        || !reader.boolean(controls.controllerInvertVertical)
        || !reader.boolean(controls.controllerAutoLookCentering)
        || !reader.boolean(controls.controllerVibration)
        || !reader.boolean(controls.controllerSwapShoulders)
        || !reader.boolean(controls.controllerInvertHorizontal)
        || !reader.integer(controls.mouseLookSensitivity)
        || !reader.boolean(controls.mouseInvertVertical)
        || !reader.boolean(controls.mouseInvertHorizontal)
        || !reader.boolean(controls.unidentifiedToggle)
        || !reader.boolean(controls.mouseAimSmoothing)
        || !reader.real(controls.adsSensitivityModifier)
        || !reader.integer(controls.doublePressDelay)) {
        return false;
    }
    auto& audio = value.audio;
    if (!reader.integer(audio.voiceOutputMode) || !reader.integer(audio.teamVoiceChannel)
        || !reader.integer(audio.reservedMode) || !reader.integer(audio.migrationVersion)
        || !reader.integer(audio.chatVolume) || !reader.boolean(audio.muteWhenUnfocused)
        || !reader.integer(audio.soundEffectsVolume) || !reader.integer(audio.dialogueVolume)
        || !reader.integer(audio.musicVolume)) {
        return false;
    }
    auto& display = value.display;
    if (!reader.integer(display.brightness) || !reader.boolean(display.showFps)
        || !reader.integer(display.hdrMode) || !reader.real(display.calibrationPrimary)
        || !reader.real(display.calibrationAlpha)) {
        return false;
    }
    auto& interfaceState = value.interface;
    if (!reader.integer(interfaceState.subtitlesMode)
        || !reader.integer(interfaceState.colorblindMode)
        || !reader.integer(interfaceState.helmetMode)
        || !reader.integer(interfaceState.hudOpacity)
        || !reader.boolean(interfaceState.displayHints)
        || !reader.integer(interfaceState.backgroundOpacity)
        || !reader.integer(interfaceState.reticleLocation)
        || !reader.integer(interfaceState.reticleColor)
        || !reader.integer(interfaceState.textSize)
        || !reader.integer(interfaceState.textColor)
        || !reader.integer(interfaceState.textBackgroundStyle)
        || !reader.integer(interfaceState.textBackgroundOpacity)
        || !reader.integer(interfaceState.reservedTextMode)
        || !reader.integer(interfaceState.subtitleOptionsEntry)) {
        return false;
    }
    auto& social = value.social;
    if (!reader.boolean(social.preferGoodConnection) || !reader.integer(social.textChatMode)
        || !reader.boolean(social.showRealNames)
        || !reader.boolean(social.clanInviteNotifications)
        || !reader.boolean(social.profanityFilter)
        || !reader.boolean(social.voiceChatEnabled)
        || !reader.integer(social.whisperChatMode)
        || !reader.integer(social.teamChatJoinMode)
        || !reader.integer(social.localChatJoinMode)
        || !reader.integer(social.clanChatJoinMode)
        || !reader.integer(social.chatAutoHideMode)
        || !reader.boolean(value.keyBindings.configured)) {
        return false;
    }
    for (auto& binding : value.keyBindings.values) {
        if (!read_optional_u16(reader, binding.primary)
            || !read_optional_u16(reader, binding.secondary)) {
            return false;
        }
    }
    return reader.boolean(value.configured);
}

[[nodiscard]] bool write_account(Writer& writer, const AccountState& account, int version) noexcept {
    if (!writer.integer(account.primarySoid)
        || !writer.integer(static_cast<std::uint32_t>(account.dismantleRewardCount))) {
        return false;
    }
    for (std::size_t index = 0; index < account.dismantleRewardCount; ++index) {
        const auto& reward = account.dismantleRewards[index];
        if (!writer.integer(reward.definitionHash) || !writer.integer(reward.quantity)) {
            return false;
        }
    }
    if (!writer.integer(static_cast<std::uint32_t>(account.profileItemCount))) {
        return false;
    }
    for (std::size_t index = 0; index < account.profileItemCount; ++index) {
        const auto& item = account.profileItems[index];
        if (!writer.integer(item.instanceSoid) || !writer.integer(item.definitionHash)
            || !writer.integer(item.quantity) || !writer.integer(item.mutationSerial)) {
            return false;
        }
    }
    if (!writer.integer(static_cast<std::uint32_t>(account.characterCount))) {
        return false;
    }
    for (std::size_t characterIndex = 0; characterIndex < account.characterCount;
         ++characterIndex) {
        const auto& character = account.characters[characterIndex];
        if (!writer.integer(character.soid) || !writer.boolean(character.selected)
            || !writer.byte(static_cast<std::uint8_t>(character.race))
            || !writer.byte(static_cast<std::uint8_t>(character.gender))
            || !writer.byte(static_cast<std::uint8_t>(character.characterClass))
            || !writer.byte(character.level) || !writer.boolean(character.accepted)
            || !writer.boolean(character.previewAvailable)
            || !writer.real(character.appearanceValue)
            || !writer.integer(character.lastOrbitedDestination)
            || !writer.boolean(character.contentBypass)
            || !writer.byte(character.movementAbilityEntry)
            || !writer.byte(character.grenadeAbilityEntry)
            || !writer.byte(character.superAbilityEntry)
            || !writer.byte(character.meleeAbilityEntry)
            || !writer.byte(character.classAbilityEntry)) {
            return false;
        }
        for (const auto& equipped : character.equipment.slots) {
            if (!writer.boolean(equipped.has_value())
                || (equipped.has_value() && !write_item(writer, *equipped))) {
                return false;
            }
        }
        if (!writer.integer(static_cast<std::uint32_t>(character.inventory.count))) {
            return false;
        }
        for (std::size_t itemIndex = 0; itemIndex < character.inventory.count; ++itemIndex) {
            if (!write_item(writer, character.inventory.values[itemIndex])) {
                return false;
            }
        }
        if (!writer.integer(character.nextInventorySerial)) {
            return false;
        }
        if (version >= kPreInputVersion) {
            if (!writer.boolean(character.hasIdentityHeader)) {
                return false;
            }
            for (std::size_t index = 0; character.hasIdentityHeader
                                        && index < character.identityHeader.size();
                 ++index) {
                if (!writer.byte(character.identityHeader[index])) {
                    return false;
                }
            }
        }
    }
    if (!write_settings(writer, account.settings)) {
        return false;
    }
    // version 4 appends the vault; the wire profile layout (v1) never carries it
    if (version >= kPreInputVersion) {
        if (!writer.integer(static_cast<std::uint32_t>(account.vault.count))) {
            return false;
        }
        for (std::size_t index = 0; index < account.vault.count; ++index) {
            if (!write_item(writer, account.vault.values[index])) {
                return false;
            }
        }
    }
    if (version >= kPreFovVersion && (!writer.boolean(account.settings.keyBindings.seeded)
        || !writer.boolean(account.settings.keyBindings.useLocalSource))) return false;
    if (version >= kPreSetupVersion && !writer.integer(account.settings.display.fieldOfView)) return false;
    if (version >= kVersion && !writer.boolean(account.settings.profileSetupCompleted)) return false;
    return true;
}

enum class HeaderLayout {
    none,
    raw,
    flagged,
};

[[nodiscard]] bool read_account(Reader& reader,
                                AccountState& account,
                                HeaderLayout layout,
                                bool hasVault) noexcept {
    std::uint32_t rewardCount = 0;
    if (!reader.integer(account.primarySoid) || !reader.integer(rewardCount)
        || rewardCount > account.dismantleRewards.size()) {
        return false;
    }
    account.dismantleRewardCount = rewardCount;
    for (std::size_t index = 0; index < account.dismantleRewardCount; ++index) {
        auto& reward = account.dismantleRewards[index];
        if (!reader.integer(reward.definitionHash) || !reader.integer(reward.quantity)) {
            return false;
        }
    }
    std::uint32_t profileCount = 0;
    if (!reader.integer(profileCount) || profileCount > account.profileItems.size()) {
        return false;
    }
    account.profileItemCount = profileCount;
    for (std::size_t index = 0; index < account.profileItemCount; ++index) {
        auto& item = account.profileItems[index];
        if (!reader.integer(item.instanceSoid) || !reader.integer(item.definitionHash)
            || !reader.integer(item.quantity) || !reader.integer(item.mutationSerial)) {
            return false;
        }
    }
    std::uint32_t characterCount = 0;
    if (!reader.integer(characterCount) || characterCount > account.characters.size()) {
        return false;
    }
    account.characterCount = characterCount;
    for (std::size_t characterIndex = 0; characterIndex < account.characterCount;
         ++characterIndex) {
        auto& character = account.characters[characterIndex];
        std::uint8_t race = 0;
        std::uint8_t gender = 0;
        std::uint8_t characterClass = 0;
        if (!reader.integer(character.soid) || !reader.boolean(character.selected)
            || !reader.integer(race)
            || race > static_cast<std::uint8_t>(dawn::state::CharacterRace::exo)
            || !reader.integer(gender)
            || gender > static_cast<std::uint8_t>(dawn::state::CharacterGender::female)
            || !reader.integer(characterClass)
            || characterClass > static_cast<std::uint8_t>(dawn::state::CharacterClass::warlock)
            || !reader.integer(character.level) || !reader.boolean(character.accepted)
            || !reader.boolean(character.previewAvailable)
            || !reader.real(character.appearanceValue)
            || !reader.integer(character.lastOrbitedDestination)
            || !reader.boolean(character.contentBypass)
            || !reader.integer(character.movementAbilityEntry)
            || !reader.integer(character.grenadeAbilityEntry)
            || !reader.integer(character.superAbilityEntry)
            || !reader.integer(character.meleeAbilityEntry)
            || !reader.integer(character.classAbilityEntry)) {
            return false;
        }
        character.race = static_cast<dawn::state::CharacterRace>(race);
        character.gender = static_cast<dawn::state::CharacterGender>(gender);
        character.characterClass = static_cast<dawn::state::CharacterClass>(characterClass);
        for (auto& equipped : character.equipment.slots) {
            bool present = false;
            if (!reader.boolean(present)) {
                return false;
            }
            if (!present) {
                equipped.reset();
                continue;
            }
            inventory::Item item{};
            if (!read_item(reader, item)) {
                return false;
            }
            equipped = item;
        }
        std::uint32_t inventoryCount = 0;
        if (!reader.integer(inventoryCount) || inventoryCount > character.inventory.values.size()) {
            return false;
        }
        character.inventory.count = inventoryCount;
        for (std::size_t itemIndex = 0; itemIndex < character.inventory.count; ++itemIndex) {
            if (!read_item(reader, character.inventory.values[itemIndex])) {
                return false;
            }
        }
        if (!reader.integer(character.nextInventorySerial)) {
            return false;
        }
        if (layout == HeaderLayout::flagged) {
            if (!reader.boolean(character.hasIdentityHeader)) {
                return false;
            }
            for (std::size_t index = 0; character.hasIdentityHeader
                                        && index < character.identityHeader.size();
                 ++index) {
                if (!reader.integer(character.identityHeader[index])) {
                    return false;
                }
            }
        } else if (layout == HeaderLayout::raw) {
            bool anySet = false;
            for (auto& value : character.identityHeader) {
                if (!reader.integer(value)) {
                    return false;
                }
                anySet = anySet || value != 0;
            }
            character.hasIdentityHeader = anySet;
            if (!anySet) {
                character.identityHeader = {};
            }
        }
    }
    if (!read_settings(reader, account.settings)) {
        return false;
    }
    if (!hasVault) {
        return true;
    }
    std::uint32_t vaultCount = 0;
    if (!reader.integer(vaultCount) || vaultCount > account.vault.values.size()) {
        return false;
    }
    account.vault.count = vaultCount;
    for (std::size_t index = 0; index < account.vault.count; ++index) {
        if (!read_item(reader, account.vault.values[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool encode(const AccountState& account, std::vector<std::byte>& output, int version) noexcept {
    if ((version != kVersion && version != kPreSetupVersion && version != kPreFovVersion
         && version != kPreInputVersion && version != kPriorVersion)
        || !dawn::state::account::valid(account)) {
        output.clear();
        return false;
    }
    try {
        output.reserve(96U * 1024U);
    } catch (...) {
        output.clear();
        return false;
    }
    Writer writer(output);
    if (!write_account(writer, account, version)) {
        output.clear();
        return false;
    }
    return true;
}

bool decode(std::span<const std::byte> input, AccountState& account, int version) noexcept {
    const auto attempt = [&](HeaderLayout layout, bool hasVault = false, bool hasInputFlags = false,
                             bool hasFov = false, bool hasSetup = false) noexcept {
        // the account image is several hundred KB, too big for the stack
        thread_local AccountState decoded{};
        decoded = {};
        Reader reader(input);
        if (!read_account(reader, decoded, layout, hasVault)
            || (hasInputFlags && (!reader.boolean(decoded.settings.keyBindings.seeded)
                || !reader.boolean(decoded.settings.keyBindings.useLocalSource)))
            || (hasFov && !reader.integer(decoded.settings.display.fieldOfView))
            || (hasSetup && !reader.boolean(decoded.settings.profileSetupCompleted)) || !reader.finished()
            || !dawn::state::account::valid(decoded)) {
            return false;
        }
        account = decoded;
        return true;
    };
    if (version == kVersion) {
        return attempt(HeaderLayout::flagged, true, true, true, true);
    }
    if (version == kPreSetupVersion) {
        return attempt(HeaderLayout::flagged, true, true, true);
    }
    if (version == kPreFovVersion) {
        return attempt(HeaderLayout::flagged, true, true);
    }
    if (version == kPreInputVersion) {
        return attempt(HeaderLayout::flagged, true);
    }
    if (version == kVaultlessVersion) {
        return attempt(HeaderLayout::flagged);
    }
    if (version == kLegacyHeaderVersion) {
        // version 2 names two layouts; only one can consume the payload exactly
        return attempt(HeaderLayout::raw) || attempt(HeaderLayout::flagged);
    }
    if (version == kPriorVersion) {
        return attempt(HeaderLayout::none);
    }
    return false;
}

bool self_test(const AccountState& account) noexcept {
    std::vector<std::byte> first;
    std::vector<std::byte> second;
    AccountState decoded{};
    return encode(account, first) && decode(first, decoded) && encode(decoded, second)
           && first == second;
}

} // namespace restoration::account_codec
