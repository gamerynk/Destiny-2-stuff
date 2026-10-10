#include <algorithm>

#include "native/activity_clock_authority.h"
#include "sensor_auth_update.h"

namespace dawn::middleware::bap::activity_message::sensor_auth_update {
namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kSlotTypeParticipation = 13;
constexpr std::uint32_t kMaximumRegion = 0x7FFFFFFF;

[[nodiscard]] bool valid_sub_blocks(std::span<const BubbleSubBlock> subBlocks) noexcept {
    if (subBlocks.size() > kBubbleSubBlockCapacity) {
        return false;
    }
    for (std::size_t index = 0; index < subBlocks.size(); ++index) {
        const BubbleSubBlock& block = subBlocks[index];
        if (block.bubble > kMaximumSubBlockBubble || block.keys.empty()
            || block.keys.size() > kBubbleKeyCapacity) {
            return false;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (subBlocks[earlier].bubble == block.bubble) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool valid(const Snapshot& snapshot) noexcept {
    if (snapshot.nativeCountdown && (!snapshot.nativeCountdownDurationTicks
        || snapshot.nativeCountdownDurationTicks > native::activity_clock::kTicksPerSecond * 86400ULL
        || snapshot.nativeCountdownRemainingTicks > snapshot.nativeCountdownDurationTicks)) return false;
    if (snapshot.unresolvedParticipationRoster) return false;
    if (snapshot.playerAssignment>30 || snapshot.playerTeamIndex>60) return false;
    if (std::find(kLifetimeStates.begin(), kLifetimeStates.end(), snapshot.lifetime)
        == kLifetimeStates.end()) {
        return false;
    }
    if (snapshot.hasRegion && snapshot.region > kMaximumRegion) {
        return false;
    }
    if (snapshot.hasSpawnOverride
        && (snapshot.spawnSliceSet > kMaximumSpawnSliceSet || snapshot.spawnSetHash == 0
            || snapshot.spawnSetHash == kAbsentSpawnSetHash)) {
        return false;
    }
    if (snapshot.missionDirectorVariant > 3) {
        return false;
    }
    // the grant is a change, not a value: the client compares it to a mirror starting at zero,
    // so a token of zero grants nothing
    if (snapshot.hasGrant
        && (snapshot.grant.bubble > kMaximumGrantBubble
            || snapshot.grant.token < kMinimumGrantToken)) {
        return false;
    }
    if (snapshot.roster.groupCount > kGroupCapacity
        || snapshot.roster.topLevelGroupCount > kTopLevelKeyCapacity
        || snapshot.roster.topLevelGroupCount > snapshot.roster.groupCount) {
        return false;
    }
    for (std::size_t group = 0; group < snapshot.roster.groupCount; ++group) {
        const Group& row = snapshot.roster.groups[group];
        if (row.slotTypes.size() != row.slotFlags.size()
            || row.slotTypes.size() != row.slotIndices.size() || row.slotTypes.empty()) {
            return false;
        }
        for (const std::uint16_t index : row.slotIndices) {
            if (index > kMaximumSlotIndex) {
                return false;
            }
        }
    }
    if (snapshot.completeParticipationRoster) {
        if (!snapshot.completeScoreboardRoster || snapshot.scoreboardPlayerCount > 12) return false;
        std::array<bool,12> slots{};
        for (std::size_t g=0; g<snapshot.roster.groupCount; ++g) {
            const auto& row=snapshot.roster.groups[g];
            if (row.key!=snapshot.roster.playerKeyGroup) continue;
            for (std::size_t s=0; s<row.slotTypes.size(); ++s) if (row.slotTypes[s]==13) {
                const auto index=row.slotIndices[s];
                if (index<7 || index>=19 || slots[index-7] || !(row.slotFlags[s]&kSlotAuthFlag)) return false;
                slots[index-7]=true;
            }
        }
        if (std::find(slots.begin(),slots.end(),false)!=slots.end()) return false;
        std::size_t occupied=0;
        for (std::size_t i=0; i<12; ++i) {
            const auto& player=snapshot.participationPlayers[i];
            if (!player.key) {
                if (player.region!=-1 || player.assignment || player.faction || !player.awaitingClientSync) return false;
                continue;
            }
            if (player.region < -1 || player.assignment>11
                || player.faction!=kFirstCrucibleFaction+player.assignment) return false;
            for (std::size_t j=0; j<i; ++j) if (snapshot.participationPlayers[j].key==player.key) return false;
            std::size_t matches=0;
            for (std::size_t j=0; j<snapshot.scoreboardPlayerCount; ++j)
                if (snapshot.scoreboardPlayers[j].key==player.key
                    && snapshot.scoreboardPlayers[j].teamIndex==player.assignment) ++matches;
            if (matches!=1) return false;
            ++occupied;
        }
        if (occupied>snapshot.scoreboardPlayerCount) return false;
    }
    return valid_sub_blocks(snapshot.roster.bubbleSubBlocks);
}

// a slot with neither auth nor sense state has no client descriptor to resolve; even an
// empty phase-2 block would make the apply loop wait on an object the client can't build
[[nodiscard]] bool write_phase_two(bits::Writer& writer, const Snapshot& snapshot) noexcept {
    bool encoded = true;
    bool keyPlaced = false;
    for (std::size_t group = 0; encoded && group < snapshot.roster.groupCount; ++group) {
        const Group& row = snapshot.roster.groups[group];
        encoded = writer.write(1, kPresenceWidth) && writer.write(row.key, kKeyWidth)
                  && writer.write(0, kKeyWidth);
        for (std::size_t slot = 0; encoded && slot < row.slotTypes.size(); ++slot) {
            if (row.slotFlags[slot] == 0) {
                continue;
            }
            const std::uint8_t slotType = row.slotTypes[slot];
            const bool firstOrEvery = !keyPlaced || snapshot.keyOnEveryParticipationSlot;
            const bool carriesPlayerKey = slotType == kSlotTypeParticipation
                                          && row.key == snapshot.roster.playerKeyGroup
                                          && firstOrEvery;
            keyPlaced = keyPlaced || carriesPlayerKey;
            if (snapshot.completeParticipationRoster && slotType == kSlotTypeParticipation
                && row.key == snapshot.roster.playerKeyGroup) {
                // authored private controller players[0..11] occupy slots 7..18; membership slot is stable,
                // score ordering and team side are not
                const auto index = row.slotIndices[slot];
                if (index < 7 || index >= 19) return false;
                const auto& player = snapshot.participationPlayers[index - 7];
                Snapshot participant = snapshot;
                participant.playerKey = player.key;
                participant.playerAssignment = player.assignment;
                participant.teamFaction = player.faction;
                participant.hasRegion = player.key && player.region >= 0;
                participant.region = participant.hasRegion ? static_cast<std::uint32_t>(player.region) : 0;
                participant.awaitClientSync = !player.key || player.awaitingClientSync;
                encoded = write_object_block(writer, participant, row.key, slotType,
                    row.slotIndices[slot], row.slotFlags[slot], true);
                continue;
            }
            encoded = write_object_block(writer,
                                         snapshot,
                                         row.key,
                                         slotType,
                                         row.slotIndices[slot],
                                         row.slotFlags[slot],
                                         carriesPlayerKey);
        }
        encoded = encoded && writer.write(0, kPresenceWidth);
    }
    return encoded;
}

[[nodiscard]] bool write_body(bits::Writer& writer, const Snapshot& snapshot) noexcept {
    // the hardwipe token is unchecked unless the client's use_hardwipe_tokens config is on
    bool encoded = writer.write(0, kHardwipeWidth)
                   && writer.write(snapshot.patchEpoch.first, kEpochWidth)
                   && writer.write(snapshot.patchEpoch.second, kEpochWidth)
                   && writer.write(snapshot.hasGrant ? 1U : 0U, kPresenceWidth);
    if (encoded && snapshot.hasGrant) {
        encoded = write_bubble_block(writer, snapshot.grant);
    }
    const std::size_t latchBit = kLatchBitWithoutGrant + (snapshot.hasGrant ? kBubbleBlockBits : 0);
    // the eight bytes after the bubble block are the native gameplay clock, copied raw (little
    // endian); zero is what every body sent before a mission owned the clock
    encoded = encoded && native::activity_clock::write_elapsed(writer, snapshot.gameplayClockTicks)
              && writer.bit_count() == latchBit;
    encoded = encoded && writer.write(1, kPresenceWidth)
              && write_roster_delta(writer, snapshot.roster, snapshot.stateSequence)
              && writer.bit_count()
                     == latchBit + 1
                            + delta_bits(snapshot.roster.topLevelGroupCount,
                                         snapshot.roster.bubbleSubBlocks);
    if (encoded && !snapshot.phaseOneOnly) {
        encoded = write_phase_two(writer, snapshot);
    }
    return encoded && writer.write(0, kPresenceWidth) && writer.write(0, kPresenceWidth);
}

} // namespace

bool encode_sensor_auth_update(const Snapshot& snapshot,
                               std::span<std::byte> output,
                               std::size_t& written) noexcept {
    written = 0;
    if (output.empty() || !valid(snapshot)) {
        return false;
    }

    // measure first: the writer fills the caller's storage as it goes, so a body that
    // doesn't fit would leave a partial one behind
    bits::Writer measure = bits::Writer::measuring();
    std::size_t required = 0;
    if (!write_body(measure, snapshot) || !measure.finish(required) || required > output.size()) {
        return false;
    }

    bits::Writer writer(output);
    std::size_t produced = 0;
    if (!write_body(writer, snapshot) || !writer.finish(produced) || produced != required) {
        return false;
    }
    written = produced;
    return true;
}

} // namespace dawn::middleware::bap::activity_message::sensor_auth_update
