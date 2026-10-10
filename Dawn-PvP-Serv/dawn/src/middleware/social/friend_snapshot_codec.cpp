#include "friend_snapshot_codec.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace dawn::middleware::social::friend_snapshot {
namespace {

constexpr std::uint32_t kMagicV2 = 0x32524652U;
constexpr std::uint32_t kMagicV3 = 0x33524652U;
constexpr std::uint32_t kMagicV4 = 0x34524652U;
constexpr std::uint16_t kVersionV2 = 2;
constexpr std::uint16_t kVersionV3 = 3;
constexpr std::uint16_t kVersionV4 = 4;
constexpr std::size_t kRecordSizeV2 = 160;
constexpr std::size_t kRecordSizeV3 = 208;

template <typename T>
void write(std::span<std::byte> output, std::size_t& offset, const T& value) noexcept {
    std::memcpy(output.data() + offset, &value, sizeof value);
    offset += sizeof value;
}

template <typename T>
void read(std::span<const std::byte> input, std::size_t& offset, T& value) noexcept {
    std::memcpy(&value, input.data() + offset, sizeof value);
    offset += sizeof value;
}

} // namespace

static_assert(sizeof(decltype(state::social::RemoteAccount{}.displayName))
              == state::social::kNameCapacity);
static_assert(sizeof(decltype(state::social::RemoteAccount{}.location))
              == state::social::kLocationCapacity);

bool encode(const state::social::Snapshot& snapshot,
            std::span<std::byte> output,
            std::size_t& written) noexcept {
    written = 0;
    const std::size_t needed = kHeaderSize + snapshot.remoteCount * kRecordSize;
    if (!state::social::valid(snapshot) || output.size() < needed) {
        return false;
    }
    std::size_t offset = 0;
    const std::uint16_t count = static_cast<std::uint16_t>(snapshot.remoteCount);
    const std::uint32_t reserved = 0;
    write(output, offset, kMagicV4);
    write(output, offset, kVersionV4);
    write(output, offset, count);
    write(output, offset, snapshot.localPlatformId);
    write(output, offset, snapshot.revision);
    std::memcpy(output.data() + offset,
                snapshot.localDisplayName.data(),
                snapshot.localDisplayName.size());
    offset += snapshot.localDisplayName.size();
    for (std::size_t index = 0; index < snapshot.remoteCount; ++index) {
        const auto& remote = snapshot.remotes[index];
        const std::int32_t relationship = static_cast<std::int32_t>(remote.relationship);
        write(output, offset, remote.platformId);
        write(output, offset, remote.accountSoid);
        write(output, offset, remote.selectedCharacterSoid);
        write(output, offset, remote.shipInstanceSoid);
        write(output, offset, remote.lobbyId);
        write(output, offset, remote.profileRevision);
        write(output, offset, relationship);
        write(output, offset, remote.personaState);
        write(output, offset, remote.appId);
        write(output, offset, remote.shipDefinitionHash);
        write(output, offset, remote.power);
        write(output, offset, remote.emblemDefinition);
        write(output, offset, remote.emblemVariant);
        write(output, offset, remote.characterLevel);
        write(output, offset, remote.characterClass);
        const std::uint8_t joinable = remote.joinable ? 1U : 0U;
        const std::uint8_t reservedByte = 0;
        write(output, offset, joinable);
        write(output, offset, reservedByte);
        write(output, offset, reserved);
        std::memcpy(output.data() + offset, remote.displayName.data(), remote.displayName.size());
        offset += remote.displayName.size();
        std::memcpy(output.data() + offset, remote.location.data(), remote.location.size());
        offset += remote.location.size();
        std::memcpy(output.data() + offset,
                    remote.steamDisplay.data(), remote.steamDisplay.size());
        offset += remote.steamDisplay.size();
        std::memcpy(output.data() + offset, remote.connect.data(), remote.connect.size());
        offset += remote.connect.size();
        std::memcpy(output.data() + offset,
                    remote.playerGroup.data(), remote.playerGroup.size());
        offset += remote.playerGroup.size();
        std::memcpy(output.data() + offset,
                    remote.playerGroupSize.data(), remote.playerGroupSize.size());
        offset += remote.playerGroupSize.size();
    }
    written = offset;
    return offset == needed;
}

bool decode(std::span<const std::byte> input, state::social::Snapshot& snapshot) noexcept {
    snapshot = {};
    if (input.size() < kHeaderSize) {
        return false;
    }
    std::size_t offset = 0;
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint16_t count{};
    read(input, offset, magic);
    read(input, offset, version);
    read(input, offset, count);
    read(input, offset, snapshot.localPlatformId);
    read(input, offset, snapshot.revision);
    std::memcpy(snapshot.localDisplayName.data(),
                input.data() + offset,
                snapshot.localDisplayName.size());
    offset += snapshot.localDisplayName.size();
    const bool legacyV2 = magic == kMagicV2 && version == kVersionV2;
    const bool legacyV3 = magic == kMagicV3 && version == kVersionV3;
    const bool currentV4 = magic == kMagicV4 && version == kVersionV4;
    const bool extended = legacyV3 || currentV4;
    const std::size_t recordSize = legacyV2 ? kRecordSizeV2
                                            : (legacyV3 ? kRecordSizeV3 : kRecordSize);
    if ((!legacyV2 && !legacyV3 && !currentV4) || count > snapshot.remotes.size()
        || input.size() != kHeaderSize + static_cast<std::size_t>(count) * recordSize) {
        snapshot = {};
        return false;
    }
    snapshot.remoteCount = count;
    for (std::size_t index = 0; index < snapshot.remoteCount; ++index) {
        auto& remote = snapshot.remotes[index];
        std::int32_t relationship{};
        std::uint32_t reserved{};
        read(input, offset, remote.platformId);
        read(input, offset, remote.accountSoid);
        if (extended) {
            read(input, offset, remote.selectedCharacterSoid);
            read(input, offset, remote.shipInstanceSoid);
            read(input, offset, remote.lobbyId);
            read(input, offset, remote.profileRevision);
        }
        read(input, offset, relationship);
        read(input, offset, remote.personaState);
        read(input, offset, remote.appId);
        if (extended) {
            read(input, offset, remote.shipDefinitionHash);
            read(input, offset, remote.power);
            read(input, offset, remote.emblemDefinition);
            read(input, offset, remote.emblemVariant);
            read(input, offset, remote.characterLevel);
            read(input, offset, remote.characterClass);
            std::uint8_t joinable{};
            std::uint8_t reservedByte{};
            read(input, offset, joinable);
            read(input, offset, reservedByte);
            remote.joinable = joinable != 0;
            read(input, offset, reserved);
            if (joinable > 1U || reservedByte != 0) {
                snapshot = {};
                return false;
            }
        } else {
            read(input, offset, reserved);
        }
        if (reserved != 0) {
            snapshot = {};
            return false;
        }
        remote.relationship = static_cast<state::social::Relationship>(relationship);
        std::memcpy(remote.displayName.data(), input.data() + offset, remote.displayName.size());
        offset += remote.displayName.size();
        std::memcpy(remote.location.data(), input.data() + offset, remote.location.size());
        offset += remote.location.size();
        if (currentV4) {
            std::memcpy(remote.steamDisplay.data(),
                        input.data() + offset, remote.steamDisplay.size());
            offset += remote.steamDisplay.size();
            std::memcpy(remote.connect.data(), input.data() + offset, remote.connect.size());
            offset += remote.connect.size();
            std::memcpy(remote.playerGroup.data(),
                        input.data() + offset, remote.playerGroup.size());
            offset += remote.playerGroup.size();
            std::memcpy(remote.playerGroupSize.data(),
                        input.data() + offset, remote.playerGroupSize.size());
            offset += remote.playerGroupSize.size();
        }
    }
    if (!state::social::valid(snapshot)) {
        snapshot = {};
        return false;
    }
    return true;
}

} // namespace dawn::middleware::social::friend_snapshot
