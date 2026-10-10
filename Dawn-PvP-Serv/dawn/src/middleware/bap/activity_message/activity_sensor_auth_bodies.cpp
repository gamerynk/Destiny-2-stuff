#include "sensor_auth_update.h"
#include "activity_player_assignment.h"
#include <bit>

#include "../../../core/runtime/runtime_policy.h"
#include "../../../state/activity/strike_pact/authority.h"
#include "../../../state/activity/coo/native_device_authority.h"

namespace dawn::middleware::bap::activity_message::sensor_auth_update {
namespace {

namespace bits = encoding::bits;

constexpr std::uint8_t kSlotTypeParticipation = 13;
constexpr std::uint8_t kSlotTypeLifetime = 17;
constexpr std::uint8_t kSlotTypeActivityScript = 18;
constexpr std::uint8_t kSlotTypeMissionDirector = 35;
constexpr std::uint8_t kSlotTypeConfiguration = 8;
constexpr std::uint8_t kSlotTypePackage = 16;
constexpr std::uint8_t kSlotTypeQueues = 41;
constexpr std::uint8_t kSlotTypeSpawnKeys = 67;

constexpr std::size_t kParticipationBits = 192;
constexpr std::size_t kParticipationRegionBits = 32;
constexpr std::size_t kLifetimeBits = 520;
constexpr std::size_t kHashSwitchBits = 32 + 1 + 32 + 32;
constexpr std::uint32_t kHashSwitchClass = 0x80800070;

[[nodiscard]] std::size_t mission_switch_count(const Snapshot& snapshot) noexcept {
    return snapshot.strike_pact.services
               ? state::activity::strike_pact::kForestHashSwitches.size()
               : 0U;
}
constexpr std::size_t kSharedMissionStateBits = 1 + 5 * 64 + 32;
constexpr std::size_t kMissionDirectorBits = 2 + 2 * 2 + kSharedMissionStateBits;
constexpr std::size_t kActivityScriptBits = kSharedMissionStateBits + 1 + 32;
// the mission director was recovered before the activity script. The runtime descriptor
// shows the shared tail ends in 32 bits, not 64; the extra word shifted lifetime
constexpr bool kInitializeActivityScript = true;
constexpr bool kInitializeMissionDirector = true;
constexpr std::size_t kConfigurationBits = 35;
constexpr std::size_t kClockVariableBits = 6 + (1 + 32) + (1 + 32);
constexpr std::uint32_t kClockVisibilityKey = 0xEF07C38B;
constexpr std::size_t kPackageBits = 7;
// in-world team key field 0x808094DD ord2: 6 bits, bias 3, so wire 3 is faction 0 (no team)
// and faction 17 is wire 20; the scoreboard's team row key has the same shape
constexpr std::uint8_t kTeamKeyWidth = 6;
constexpr std::uint32_t kTeamKeyBias = 3;
static_assert(kFirstCrucibleFaction + kCrucibleTeamCount - 1 + kTeamKeyBias < (1U << kTeamKeyWidth));
// scoreboard, schema 0x808099F9. A field costs a leading presence bit iff its flags
// carry 0x02, which the empty 7-bit body and the 386-bit type-18 body both force
constexpr std::size_t kScoreboardPlayerRowBits = (1 + 64) + 3 + (1 + 6) + 1;
constexpr std::size_t kScoreboardTeamRowBits = (1 + kTeamKeyWidth) * 2 + 1 + 1;
constexpr std::size_t kScoreboardBits =
    1 + 5 + kScoreboardPlayerRowBits + 1 + 4 + kCrucibleTeamCount * kScoreboardTeamRowBits + 5;
static_assert(kScoreboardBits == 124, "player table + team table; recheck the flag-0x02 rule");
constexpr std::size_t kScoreboardPlayerCountMaximum = (1U << 5U) - 1U;
constexpr std::size_t kScoreboardTeamCountMaximum = (1U << 4U) - 1U;
static_assert(kMaximumCrucibleScoreboardPlayers <= kScoreboardPlayerCountMaximum);
static_assert(kMaximumCrucibleScoreboardPlayers <= kScoreboardTeamCountMaximum);
[[nodiscard]] bool complete_scoreboard(const Snapshot& snapshot) noexcept {
    if (!snapshot.competitiveScoreboard || !snapshot.completeScoreboardRoster
        || snapshot.scoreboardPlayerCount == 0
        || snapshot.scoreboardPlayerCount > snapshot.scoreboardPlayers.size()
        || snapshot.scoreboardTeamCount == 0
        || snapshot.scoreboardTeamCount > kMaximumCrucibleScoreboardPlayers
        || snapshot.playerTeamIndex >= snapshot.scoreboardTeamCount
        || snapshot.teamFaction != kFirstCrucibleFaction + snapshot.playerTeamIndex) {
        return false;
    }
    bool containsLocalPlayer = false;
    for (std::size_t index = 0; index < snapshot.scoreboardPlayerCount; ++index) {
        const auto& player = snapshot.scoreboardPlayers[index];
        if (player.key == 0 || player.teamIndex >= snapshot.scoreboardTeamCount
            || player.headline < -1 || player.headline > 21) return false;
        containsLocalPlayer |= player.key == snapshot.playerKey
            && player.teamIndex == snapshot.playerTeamIndex;
        for (std::size_t other = 0; other < index; ++other) {
            if (snapshot.scoreboardPlayers[other].key == player.key) return false;
        }
    }
    return containsLocalPlayer;
}
[[nodiscard]] bool two_team_scoreboard(const Snapshot& snapshot) noexcept {
    return snapshot.competitiveScoreboard && !snapshot.completeScoreboardRoster
           && snapshot.playerTeamIndex < kCrucibleTeamCount
           && snapshot.teamFaction == kFirstCrucibleFaction + snapshot.playerTeamIndex;
}
[[nodiscard]] std::size_t scoreboard_bits(const Snapshot& snapshot) noexcept {
    if (complete_scoreboard(snapshot)) {
        return 1 + 5 + snapshot.scoreboardPlayerCount * (kScoreboardPlayerRowBits + (snapshot.scoreboardStats ? 7+8+3*32 : 0))
               + 1 + 4 + snapshot.scoreboardTeamCount * (kScoreboardTeamRowBits + (snapshot.scoreboardStats ? 5+4+3*32 : 0))
               + 5 + (snapshot.scoreboardLayout ? 32 : 0);
    }
    return two_team_scoreboard(snapshot) ? kScoreboardBits : kPackageBits;
}
constexpr std::size_t kQueueBits = 12;
constexpr std::size_t kSpawnKeyBits = 32 * 32 + 1 + 32;

constexpr std::uint32_t kSignedZero = 0x80000000;
constexpr std::uint32_t kSignedMinusOne = 0x7FFFFFFF;
constexpr std::uint32_t kRegionBias = 0x80000000;
// native 0x808099C4 construction gives a 365-day horizon: 31,536,000 s through Destiny's
// 673,200 ticks/s float path truncates to this exact u64
constexpr std::uint64_t kMissionStateHorizon = 0x0000134F00C00000ULL;
constexpr std::uint32_t kAwaitingClientSync = 2;
constexpr std::uint32_t kWaitingSwitchKey = 0xB3C1251B;
constexpr std::uint32_t kWaitingSwitchClass = 0x80800007;
constexpr std::size_t kSpawnOverrideCount = 3;
constexpr std::uint8_t kSpawnOverrideIndexWidth = 10;
constexpr std::uint32_t kSpawnOverrideIndexBias = 1;
constexpr std::size_t kSpawnKeyCount = 32;

// zero-fill is not safe here: every biased field must carry its bias, or a stored zero
// decodes to the smallest signed value
[[nodiscard]] bool write_participation(bits::Writer& writer, const Snapshot& snapshot) noexcept {
    // an optional field's value follows its presence bit, so sending +0 shifts everything below
    bool encoded = writer.write(snapshot.hasRegion ? 1U : 0U, kPresenceWidth);
    if (encoded && snapshot.hasRegion) {
        encoded = writer.write(kRegionBias + snapshot.region, kParticipationRegionBits);
    }
    return encoded && writer.write(0, kPresenceWidth) && writer.write(1, kPresenceWidth)
           && writer.write(1, kPresenceWidth) && writer.write(1, kPresenceWidth)
           && writer.write(0, kPresenceWidth) && writer.write(1, 3) && writer.write(1, 2)
           && writer.write(0, 3) && writer.write(0, 32)
           && writer.write(snapshot.playerAssignment + player_assignment::kBias,
                           player_assignment::kAuthorityWidth)
           && writer.write(0, kPresenceWidth) && writer.write(0, 3)
           && writer.write(1, kPresenceWidth) && writer.write(snapshot.playerKey, 64)
           && writer.write(0, 5)
           // 808094DD ord2 is the combat faction read by 4B7650/A56070, same as the native
           // scoreboard team key
           && writer.write(snapshot.teamFaction + kTeamKeyBias, kTeamKeyWidth)
           && writer.write(0, 6)
           && writer.write(0, 6)
           // byte 736 bypasses DC3770's native death timer, so Crucible uses the authored respawn
           // delay (DC1A20); keep the PvE fallback when it is -1. Byte 737 is initial spawning
           && writer.write(snapshot.honorNativeRespawnDelay ? 0U : 1U, kPresenceWidth)
           && writer.write(snapshot.awaitClientSync ? kAwaitingClientSync : 0U, 4)
           && writer.write(0, 3) && writer.write(0, kPresenceWidth) && writer.write(128, 8)
           && writer.write(kSignedZero, 32);
}

[[nodiscard]] bool write_lifetime(bits::Writer& writer, const Snapshot& snapshot) noexcept {
    const bool completed = snapshot.missionCompletion.valid();
    const std::uint32_t lifetime = completed ? std::uint32_t{snapshot.missionCompletion.state}
                                             : std::uint32_t{snapshot.lifetime};
    bool encoded = writer.write(lifetime + 1, 4) && writer.write(completed ? 2U : 1U, 3)
                   // schema 0x8080991A field2 is the unconditional byte2 bool; ABC090 copies it to lifetime
                   // component+182, 4FFD80 reads it and D4B870 derives the desired-join-controls restriction
                   && writer.write(snapshot.allowJoinInProgress ? 1U : 0U, kPresenceWidth)
                   && writer.write(kSignedZero, 32)
                   && writer.write(0, 32) && writer.write(kSignedZero, 32)
                   && writer.write(1 + mission_switch_count(snapshot), 6)
                   && writer.write(kWaitingSwitchKey, 32) && writer.write(1, kPresenceWidth)
                   && writer.write(kWaitingSwitchClass, 32) && writer.write(kSignedZero, 32);
    if (snapshot.strike_pact.services) {
        for (const auto& row : state::activity::strike_pact::kForestHashSwitches) {
            encoded = encoded && writer.write(row.key, 32) && writer.write(1, kPresenceWidth)
                      && writer.write(kHashSwitchClass, 32) && writer.write(row.value, 32);
        }
    }
    encoded = encoded && writer.write(kSignedZero, 32);
    for (std::size_t index = 0; encoded && index < kSpawnOverrideCount; ++index) {
        const std::uint32_t slice =
            snapshot.hasSpawnOverride ? snapshot.spawnSliceSet + kSpawnOverrideIndexBias : 0U;
        const std::uint32_t hash =
            snapshot.hasSpawnOverride ? snapshot.spawnSetHash : kAbsentSpawnSetHash;
        encoded = writer.write(slice, kSpawnOverrideIndexWidth) && writer.write(hash, 32);
    }
    // struct +1256 is the out-of-bounds activity_quarantine selector; the reader arms it
    // at or below 0x3F unsigned, so minus one leaves it clear and teleports nobody
    return encoded && writer.write(0, kPresenceWidth) && writer.write(0, 32)
           && writer.write(kSignedMinusOne, 32) && writer.write(0, 32)
           && writer.write(kSlotTypeBias, kSlotTypeWidth)
           && writer.write(kSlotIndexBias, kSlotIndexWidth) && writer.write(0, 32)
           && writer.write(0, 3);
}

[[nodiscard]] bool write_spawn_keys(bits::Writer& writer) noexcept {
    bool encoded = true;
    for (std::size_t index = 0; encoded && index < kSpawnKeyCount; ++index) {
        encoded = writer.write(kSignedZero + index, 32);
    }
    return encoded && writer.write(0, kPresenceWidth) && writer.write(kSignedMinusOne, 32);
}

[[nodiscard]] bool write_scoreboard(bits::Writer& writer, const Snapshot& snapshot) noexcept {
    const bool complete = complete_scoreboard(snapshot);
    const std::size_t playerCount = complete ? snapshot.scoreboardPlayerCount : 1;
    const std::size_t teamCount = complete ? snapshot.scoreboardTeamCount : kCrucibleTeamCount;
    bool encoded = writer.write(1, kPresenceWidth) && writer.write(playerCount, 5);
    for (std::size_t row = 0; encoded && row < playerCount; ++row) {
        const std::uint64_t key = complete ? snapshot.scoreboardPlayers[row].key : snapshot.playerKey;
        const auto headline = complete ? snapshot.scoreboardPlayers[row].headline : -1;
        encoded = writer.write(1, kPresenceWidth) && writer.write(key, 64)
                  && writer.write(0, kPresenceWidth) && writer.write(0, kPresenceWidth)
                  && writer.write(complete && snapshot.scoreboardStats ? 1U : 0U, kPresenceWidth);
        if (complete && snapshot.scoreboardStats) {
            const auto& stats=snapshot.scoreboardPlayers[row];
            encoded=encoded && writer.write(8,7);
            for (unsigned column=0;encoded && column<8;++column) {
                const bool present=column==0 || column==5 || column==7;
                encoded=writer.write(present?1U:0U,1);
                if (encoded && present) {
                    const float value=static_cast<float>(column==0?stats.score:column==5?stats.kills:stats.deaths);
                    encoded=writer.write(std::bit_cast<std::uint32_t>(value),32);
                }
            }
        }
        encoded=encoded && writer.write(1,kPresenceWidth) && writer.write(headline+1,6)
            && writer.write(1,kPresenceWidth);
    }
    encoded = encoded && writer.write(1, kPresenceWidth) && writer.write(teamCount, 4);
    for (std::size_t row = 0; encoded && row < teamCount; ++row) {
        bool occupied = false;
        for (std::size_t player = 0; complete && player < playerCount; ++player)
            occupied |= snapshot.scoreboardPlayers[player].teamIndex == row;
        encoded = writer.write(1, kPresenceWidth)
                  && writer.write(kFirstCrucibleFaction + row + kTeamKeyBias, kTeamKeyWidth)
                  && writer.write(1, kPresenceWidth) && writer.write(complete ? 0U : 1U, kTeamKeyWidth)
                  && writer.write(occupied ? 1U : 0U, kPresenceWidth)
                  && writer.write(complete && snapshot.scoreboardStats ? 1U : 0U,kPresenceWidth);
        if (complete && snapshot.scoreboardStats) {
            const auto& stats=snapshot.scoreboardTeams[row];
            encoded=encoded && writer.write(4,5);
            for (unsigned column=0;encoded && column<4;++column) {
                const bool present=column!=1;
                encoded=writer.write(present?1U:0U,1);
                if (encoded && present) {
                    const float value=static_cast<float>(column==0?stats.score:column==2?stats.kills:stats.deaths);
                    encoded=writer.write(std::bit_cast<std::uint32_t>(value),32);
                }
            }
        }
    }
    // root ord2/3 stay absent. Ord4 is the layout selector at +16F4, matched by FD96F0 against
    // 80809A1F layouts; absent, it kept 811C9DC5 and gave zero result columns
    const bool layout = complete && snapshot.scoreboardLayout != 0;
    encoded = encoded && pad_bits(writer, 2) && writer.write(layout ? 1U : 0U, 1);
    if (encoded && layout) encoded = writer.write(snapshot.scoreboardLayout, 32);
    return encoded && pad_bits(writer, 2);
}

[[nodiscard]] bool write_shared_mission_state(bits::Writer& writer, bool active) noexcept {
    // the five wide fields start with a zero lower bound and the native one-year upper horizon;
    // an all-zero tail overwrote the upper bound and left an active script with an empty window
    return writer.write(active ? 1U : 0U, kPresenceWidth) && writer.write(0, 64)
           && writer.write(kMissionStateHorizon, 64) && writer.write(0, 64)
           && writer.write(0, 64) && writer.write(0, 64) && writer.write(0, 32);
}

[[nodiscard]] bool write_mission_director(bits::Writer& writer,
                                          const Snapshot& snapshot) noexcept {
    return writer.write(snapshot.missionDirectorVariant & 1U, kPresenceWidth)
           && writer.write((snapshot.missionDirectorVariant >> 1U) & 1U, kPresenceWidth)
           && writer.write(1, 2) && writer.write(1, 2)
           && write_shared_mission_state(writer, snapshot.missionDirectorActive);
}

[[nodiscard]] bool write_activity_script(bits::Writer& writer,
                                         const Snapshot& snapshot) noexcept {
    bool state = true;
    if (snapshot.nativeCountdown) {
        state = writer.write(1, 1) && writer.write(0, 64)
            && writer.write(snapshot.nativeCountdownDurationTicks, 64) && writer.write(0, 64)
            && writer.write(snapshot.nativeCountdownRemainingTicks, 64) && writer.write(snapshot.nativeCountdownAnchorTicks, 64)
            && writer.write(std::bit_cast<std::uint32_t>(snapshot.nativeCountdownRunning ? 1.0F : 0.0F), 32);
    } else state = write_shared_mission_state(writer, true);
    return state
           && writer.write(snapshot.activityScriptFlag ? 1U : 0U, kPresenceWidth)
           && writer.write(kSignedZero
                               + static_cast<std::uint32_t>(snapshot.activityScriptState),
                           32);
}

} // namespace

std::size_t auth_body_bits(const Snapshot& snapshot,
                           std::uint32_t key,
                           std::uint8_t slotType,
                           std::uint16_t slotIndex,
                           bool carriesPlayerKey) noexcept {
    if constexpr (core::runtime::policy::kActivityBackendReplication) {
        if (const std::size_t mission = state::activity::strike_pact::body_bits(
                snapshot.strike_pact, key, slotType, slotIndex)) {
            return mission;
        }
        if (snapshot.cruciblePlacements.owns(key, slotType, slotIndex)) return 252;
        if (slotType == kSlotTypeParticipation) {
            return carriesPlayerKey
                   ? kParticipationBits + (snapshot.hasRegion ? kParticipationRegionBits : 0)
                   : 0;
        }
        if (slotType == kSlotTypeLifetime) {
            return kLifetimeBits + mission_switch_count(snapshot) * kHashSwitchBits;
        }
        if (slotType == kSlotTypeActivityScript && kInitializeActivityScript) {
            return kActivityScriptBits;
        }
        if (slotType == kSlotTypeMissionDirector && kInitializeMissionDirector) {
            return kMissionDirectorBits;
        }
        if (slotType == kSlotTypeConfiguration) {
            return kConfigurationBits + (snapshot.nativeClockVisible ? kClockVariableBits : 0);
        }
        if (slotType == kSlotTypePackage) {
            return scoreboard_bits(snapshot);
        }
        if (slotType == kSlotTypeQueues) {
            return kQueueBits;
        }
        if (slotType == kSlotTypeSpawnKeys) {
            return kSpawnKeyBits;
        }
    }
    return 0;
}

bool write_auth_body(bits::Writer& writer,
                      const Snapshot& snapshot,
                      std::uint32_t key,
                      std::uint8_t slotType,
                      std::uint16_t slotIndex,
                      bool carriesPlayerKey) noexcept {
    if constexpr (core::runtime::policy::kActivityBackendReplication) {
        const std::size_t start = writer.bit_count();
        const std::size_t expected =
            auth_body_bits(snapshot, key, slotType, slotIndex, carriesPlayerKey);
        bool encoded = true;
        if (state::activity::strike_pact::body_bits(snapshot.strike_pact, key, slotType, slotIndex)) {
            encoded = state::activity::strike_pact::write_body(
                writer, snapshot.strike_pact, key, slotType, slotIndex);
        } else if (snapshot.cruciblePlacements.owns(key, slotType, slotIndex)) {
            encoded = state::activity::coo::native_device::object(
                writer, snapshot.cruciblePlacements.generation, true);
        } else if (slotType == kSlotTypeParticipation && carriesPlayerKey) {
            encoded = write_participation(writer, snapshot);
        } else if (slotType == kSlotTypeLifetime) {
            encoded = write_lifetime(writer, snapshot);
        } else if (slotType == kSlotTypeActivityScript && kInitializeActivityScript) {
            encoded = write_activity_script(writer, snapshot);
        } else if (slotType == kSlotTypeMissionDirector && kInitializeMissionDirector) {
            encoded = write_mission_director(writer, snapshot);
        } else if (slotType == kSlotTypeConfiguration) {
            // 80804F10 byte0 is a bool; the optional array at +4 holds a count and key/float pairs,
            // at component auth base+180 (count+184, key+188, float+18C) for 103DE20 and 13E0250
            encoded = writer.write(0, kPresenceWidth)
                      && writer.write(snapshot.nativeClockVisible ? 1U : 0U, kPresenceWidth);
            if (encoded && snapshot.nativeClockVisible) {
                encoded = writer.write(1, 6)
                          && writer.write(1, kPresenceWidth) && writer.write(kClockVisibilityKey, 32)
                          && writer.write(1, kPresenceWidth) && writer.write(std::bit_cast<std::uint32_t>(1.0F), 32);
            }
            encoded = encoded && writer.write(0, kPresenceWidth) && writer.write(0, 32);
        } else if (slotType == kSlotTypePackage
                   && (complete_scoreboard(snapshot) || two_team_scoreboard(snapshot))) {
            encoded = write_scoreboard(writer, snapshot);
        } else if (slotType == kSlotTypePackage) {
            encoded = pad_bits(writer, kPackageBits);
        } else if (slotType == kSlotTypeQueues) {
            encoded = writer.write(0, 7) && writer.write(0, 5);
        } else if (slotType == kSlotTypeSpawnKeys) {
            encoded = write_spawn_keys(writer);
        }
        return encoded && writer.bit_count() == start + expected;
    } else {
        return true;
    }
}

} // namespace dawn::middleware::bap::activity_message::sensor_auth_update
