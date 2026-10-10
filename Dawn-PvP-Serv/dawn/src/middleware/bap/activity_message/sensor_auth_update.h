#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "../../encoding/bit_writer.h"
#include "activity_patch_epoch_parser.h"
#include "../../../state/activity/coo/lifecycle_service.h"
#include "../../../state/activity/strike_pact/frame.h"
#include "../../../state/activity/crucible/placement_frame.h"

namespace dawn::middleware::bap::activity_message::sensor_auth_update {

inline constexpr std::uint32_t kMessageType = 5;
inline constexpr std::size_t kAuthoritySlotCount = 65;
// bubble 64 is in the table but the world controller can't enter it, so it is never granted
inline constexpr std::uint8_t kMaximumGrantBubble = 63;
// a grant token of zero equals the client's cleared mirror, so it grants nothing
inline constexpr std::uint16_t kMinimumGrantToken = 1;
// a mission publishes its whole authored roster at once; this bound matches the production
// publisher, and per-bubble declarations keep their own native masks
inline constexpr std::size_t kGroupCapacity = 256;
inline constexpr std::size_t kTopLevelKeyCapacity = 256;
// the three lifetime states spawn gate G4's unbounded jump table accepts
inline constexpr std::array<std::uint8_t, 3> kLifetimeStates = {3, 6, 10};
inline constexpr std::uint8_t kSlotSenseFlag = 1;
inline constexpr std::uint8_t kSlotAuthFlag = 2;
inline constexpr std::uint32_t kMaximumSpawnSliceSet = 0x1FF;
// the unset spawn-set hash; an override carrying it disables the override it was
// meant to arm
inline constexpr std::uint32_t kAbsentSpawnSetHash = 0x811C9DC5;
// derived from the client: spawn predicate maps faction - 17 through an identity table
// when <= 11 unsigned, so 17 is default_team1_*, 18 team2; 16 underflows, no spawn match
inline constexpr std::uint8_t kFirstCrucibleFaction = 17;
inline constexpr std::uint8_t kCrucibleTeamCount = 2;
inline constexpr std::size_t kMaximumCrucibleScoreboardPlayers = 12;

struct ScoreboardPlayer final {
    std::uint64_t key{};
    std::uint8_t teamIndex{};
    std::uint32_t score{}, kills{}, deaths{};
    // native auth row+12C selects an authored headline; it is not a team index
    std::int8_t headline{-1};
};
struct ScoreboardTeam final { std::uint32_t score{}, kills{}, deaths{}; };

struct Grant final {
    std::uint8_t bubble{};
    std::uint16_t token{};
};

// descriptor ordinals are not assumed to equal the actual slot index
struct Group final {
    std::uint32_t key{};
    std::span<const std::uint8_t> slotTypes{};
    std::span<const std::uint8_t> slotFlags{};
    std::span<const std::uint16_t> slotIndices{};
};

inline constexpr std::size_t kBubbleSubBlockCapacity = 64;
inline constexpr std::size_t kBubbleKeyCapacity = 96;
inline constexpr std::uint32_t kMaximumSubBlockBubble = 63;

struct BubbleSubBlock final {
    std::uint32_t bubble{};
    std::span<const std::uint32_t> keys{};
};

struct Roster final {
    std::array<Group, kGroupCapacity> groups{};
    std::size_t groupCount{};
    std::size_t topLevelGroupCount{};
    // group whose first type-13 block carries the player key; it must be one that registers
    std::uint32_t playerKeyGroup{};
    std::span<const BubbleSubBlock> bubbleSubBlocks{};
};

struct Snapshot final {
    // message 52's payload, echoed exactly; a wrong epoch skips phase 2 and reports nothing
    patch_epoch::PatchEpoch patchEpoch{};
    Roster roster{};
    Grant grant{};
    std::uint64_t playerKey{};
    std::uint8_t playerAssignment{};
    std::uint8_t playerTeamIndex{};
    std::uint8_t stateSequence{};
    std::uint32_t region{};
    std::uint32_t spawnSetHash{};
    std::uint32_t spawnSliceSet{};
    std::uint8_t lifetime{};
    bool allowJoinInProgress{};
    std::uint8_t missionDirectorVariant{};
    bool missionDirectorActive{};
    bool missionDirectorTransition{};
    bool activityScriptFlag{};
    std::int32_t activityScriptState{};
    bool hasGrant{};
    bool hasRegion{};
    bool hasSpawnOverride{};
    bool awaitClientSync{};
    bool phaseOneOnly{};
    // fill the participation body on every type-13 slot, not just the first: the gate reads
    // the record of the object the player datum names, which may not be the first
    bool keyOnEveryParticipationSlot{};
    // in-world faction for the type-13 player record (0x808094DD ord2); zero is no team, which
    // every non-PvP destination carries
    std::uint8_t teamFaction{};
    bool competitiveScoreboard{};
    bool honorNativeRespawnDelay{};
    struct ParticipationPlayer final {
        std::uint64_t key{};
        std::int32_t region{-1};
        std::uint8_t assignment{}, faction{};
        bool awaitingClientSync{true};
    };
    bool completeParticipationRoster{};
    // authenticated peer read failed; reject the entire update and retry
    bool unresolvedParticipationRoster{};
    std::array<ParticipationPlayer, 12> participationPlayers{};
    bool completeScoreboardRoster{};
    std::uint8_t scoreboardPlayerCount{};
    std::uint8_t scoreboardTeamCount{};
    std::array<ScoreboardPlayer, kMaximumCrucibleScoreboardPlayers> scoreboardPlayers{};
    bool scoreboardStats{};
    // native 808099F9 ord4 selects an authored 80809A1F column layout; zero keeps defaults
    std::uint32_t scoreboardLayout{};
    bool nativeClockVisible{};
    bool nativeCountdown{}, nativeCountdownRunning{};
    std::uint64_t nativeCountdownDurationTicks{}, nativeCountdownRemainingTicks{}, nativeCountdownAnchorTicks{};
    std::array<ScoreboardTeam, kMaximumCrucibleScoreboardPlayers> scoreboardTeams{};
    state::activity::crucible::PlacementFrame cruciblePlacements{};
    state::activity::strike_pact::Frame strike_pact{};
    state::activity::coo::CompletionPublication missionCompletion{};
    // native gameplay clock base in 673,200 ticks/s, carried in the eight raw bytes after the
    // bubble block; zero is what every non-mission body already sent there
    std::uint64_t gameplayClockTicks{};
};

// phase 2 has no resync point, so a one-bit slip corrupts every later block silently; every
// writer checks its own end position and the encode fails rather than ship a slipped body
[[nodiscard]] bool encode_sensor_auth_update(const Snapshot& snapshot,
                                             std::span<std::byte> output,
                                             std::size_t& written) noexcept;

inline constexpr std::size_t kLatchBitWithoutGrant = 201;
inline constexpr std::size_t kBubbleBlockBits =
    kAuthoritySlotCount + 2 + 3 * kAuthoritySlotCount + 16;
inline constexpr std::uint8_t kEpochWidth = 64;
inline constexpr std::uint8_t kHardwipeWidth = 8;
inline constexpr std::uint8_t kActivityTokenWidth = 64;
inline constexpr std::uint8_t kGrantTokenWidth = 16;
inline constexpr std::uint8_t kPresenceWidth = 1;
inline constexpr std::uint8_t kDeltaCountWidth = 9;
inline constexpr std::size_t kDeltaKeysBit = 12;
inline constexpr std::size_t kDeltaMaskWords = 8;
inline constexpr std::uint8_t kKeyWidth = 32;
inline constexpr std::uint8_t kSlotTypeWidth = 7;
inline constexpr std::uint8_t kSlotIndexWidth = 16;
inline constexpr std::uint32_t kSlotTypeBias = 1;
inline constexpr std::uint32_t kSlotIndexBias = 32768;
inline constexpr std::uint16_t kMaximumSlotIndex = 32767;
inline constexpr std::uint32_t kStateByteBias = 0x80;

[[nodiscard]] constexpr std::size_t delta_mask_bit(std::size_t keyCount) noexcept {
    return kDeltaKeysBit + 32 * keyCount + 1;
}

[[nodiscard]] constexpr std::size_t delta_state_count_bit(std::size_t keyCount) noexcept {
    return delta_mask_bit(keyCount) + 32 * kDeltaMaskWords + 1;
}

inline constexpr std::uint8_t kBubbleCountWidth = 7;
inline constexpr std::uint32_t kBubbleKeyBias = 0x80000000;
inline constexpr std::size_t kBubbleMaskWords = 3;
inline constexpr std::size_t kBubbleSubBlockFixedBits =
    kPresenceWidth + kKeyWidth + kPresenceWidth + kPresenceWidth + kBubbleCountWidth
    + kPresenceWidth + 32 * kBubbleMaskWords + kPresenceWidth + kBubbleCountWidth;
inline constexpr std::size_t kBubbleSubBlockKeyBits = kKeyWidth + 8;

[[nodiscard]] constexpr std::size_t
bubble_bits(std::span<const BubbleSubBlock> subBlocks) noexcept {
    std::size_t bits = kBubbleCountWidth;
    for (const BubbleSubBlock& block : subBlocks) {
        bits += kBubbleSubBlockFixedBits + kBubbleSubBlockKeyBits * block.keys.size();
    }
    return bits;
}

[[nodiscard]] constexpr std::size_t delta_bits(
    std::size_t keyCount,
    std::span<const BubbleSubBlock> subBlocks) noexcept {
    return delta_state_count_bit(keyCount) + kDeltaCountWidth + 8 * keyCount + 1
           + (subBlocks.empty() ? 0 : bubble_bits(subBlocks));
}

[[nodiscard]] bool pad_bits(encoding::bits::Writer& writer, std::size_t count) noexcept;

[[nodiscard]] bool write_bubble_block(encoding::bits::Writer& writer, const Grant& grant) noexcept;

[[nodiscard]] bool write_roster_delta(encoding::bits::Writer& writer,
                                      const Roster& roster,
                                      std::uint8_t stateSequence) noexcept;

[[nodiscard]] std::size_t
auth_body_bits(const Snapshot& snapshot,
               std::uint32_t key,
               std::uint8_t slotType,
               std::uint16_t slotIndex,
               bool carriesPlayerKey) noexcept;

[[nodiscard]] bool write_auth_body(encoding::bits::Writer& writer,
                                   const Snapshot& snapshot,
                                   std::uint32_t key,
                                   std::uint8_t slotType,
                                   std::uint16_t slotIndex,
                                   bool carriesPlayerKey) noexcept;

// the 32-bit length counts the remainder (reset bit, auth root bit, sense bit); leaving the
// reset bit out desyncs by 3 bits with nothing reported
[[nodiscard]] bool write_object_block(encoding::bits::Writer& writer,
                                      const Snapshot& snapshot,
                                      std::uint32_t key,
                                      std::uint8_t slotType,
                                      std::uint16_t slotIndex,
                                      std::uint8_t flags,
                                      bool carriesPlayerKey) noexcept;

} // namespace dawn::middleware::bap::activity_message::sensor_auth_update
