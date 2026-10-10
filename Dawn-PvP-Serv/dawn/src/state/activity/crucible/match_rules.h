#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace dawn::state::activity::crucible::match_rules {

inline constexpr std::size_t kMaximumPlayers = 12;
inline constexpr std::size_t kMaximumSides = 12;
inline constexpr std::size_t kMaximumZones = 8;
inline constexpr std::size_t kMaximumCrests = 64;
inline constexpr std::uint8_t kNoSide = 0xFF;

enum class Mode : std::uint8_t {
    control, clash, rumble, survival, countdown, supremacy, breakthrough,
    mayhem, lockdown, showdown, scorched, elimination, doubles, momentum,
    controlIronBanner, doublesCrimson, scorchedTeam, demolition, hardware,
};
enum class Family : std::uint8_t {
    kills, zones, crests, elimination, survival, charge, showdown, breakthrough,
};
enum class SideModel : std::uint8_t { teams, freeForAll };

[[nodiscard]] std::optional<Mode> find_mode(std::string_view symbolicName) noexcept;
[[nodiscard]] std::string_view symbolic_name(Mode mode) noexcept;
[[nodiscard]] Family family_for(Mode mode) noexcept;

struct Rules final {
    Mode mode{};
    SideModel sideModel{SideModel::teams};
    std::uint8_t sideCount{};
    std::uint8_t zoneCount{};
    std::uint8_t teamReserves{};
    std::uint8_t objectivesToWin{};
    std::uint16_t roundsToWin{};
    std::uint16_t maximumRounds{};
    std::uint32_t scoreLimit{};
    std::uint32_t roundScoreLimit{};
    std::uint32_t killPoints{};
    std::uint32_t crestPoints{};
    std::uint32_t crestDenyPoints{};
    std::uint64_t matchDurationMs{};
    std::uint64_t roundDurationMs{};
    bool allowUnverifiedModel{};
    bool allowUnopposed{};
};

struct Player final { std::uint64_t id{}; std::uint8_t side{}; };
struct Kill final { std::uint64_t killer{}, victim{}; };
struct ZoneTaken final { std::uint8_t zone{}; std::uint64_t actor{}; };
struct ZoneCleared final { std::uint8_t zone{}; std::uint64_t actor{}; };
struct CrestDropped final { std::uint64_t crest{}, victim{}; };
struct CrestCollected final { std::uint64_t crest{}, collector{}; };
struct CrestExpired final { std::uint64_t crest{}; };
struct Revived final { std::uint64_t player{}; };
struct ChargeArmed final { std::uint64_t charge{}, actor{}; };
struct ChargeDefused final { std::uint64_t charge{}, actor{}; };
struct ChargeDetonated final { std::uint64_t charge{}, actor{}; };
struct ObjectiveCaptured final { std::uint8_t objective{}; std::uint64_t actor{}; };
struct ObjectiveDefended final { std::uint8_t objective{}; std::uint64_t actor{}; };
struct RoundAwarded final { std::uint8_t side{}; };
using Action = std::variant<Kill, ZoneTaken, ZoneCleared, CrestDropped, CrestCollected,
                            CrestExpired, Revived, ChargeArmed, ChargeDefused,
                            ChargeDetonated, ObjectiveCaptured, ObjectiveDefended, RoundAwarded>;
struct Event final { std::uint64_t serial{}; Action action{}; };

enum class Status : std::uint8_t { applied, invalid, stale, inactive, ended };
enum class Finish : std::uint8_t { none, score, rounds, time, draw };

struct SideState final {
    std::uint32_t score{};
    std::uint32_t roundScore{};
    std::uint16_t roundWins{};
    std::uint8_t teamReserves{};
};
struct PlayerState final {
    Player player{};
    bool alive{};
    std::uint8_t lastDefeatSide{kNoSide};
    std::uint32_t lastDefeatPoints{};
};
struct State final {
    std::uint64_t lobbyId{};
    std::uint64_t nowMs{};
    std::uint64_t lastSerial{};
    std::uint16_t roundsStarted{};
    std::uint8_t sideCount{};
    std::uint8_t playerCount{};
    std::uint8_t activeCrests{};
    std::uint8_t objectiveProgress{};
    std::uint8_t winner{kNoSide};
    bool active{};
    bool roundActive{};
    Finish finish{Finish::none};
    std::array<SideState, kMaximumSides> sides{};
    std::array<PlayerState, kMaximumPlayers> players{};
    std::array<std::uint8_t, kMaximumZones> zoneOwners{};
};

class Match final {
public:
    [[nodiscard]] bool start(std::uint64_t lobbyId, Rules rules,
                             std::span<const Player> roster) noexcept;
    [[nodiscard]] bool begin_round(std::uint8_t attackingSide = kNoSide) noexcept;
    [[nodiscard]] Status advance(std::uint64_t elapsedMs) noexcept;
    [[nodiscard]] Status apply(const Event& event) noexcept;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] const Rules& rules() const noexcept { return rules_; }
private:
    struct Crest final { std::uint64_t id{}; std::uint8_t victimSide{}; bool active{}; };
    [[nodiscard]] PlayerState* player(std::uint64_t id) noexcept;
    [[nodiscard]] const PlayerState* player(std::uint64_t id) const noexcept;
    [[nodiscard]] Status apply_action(const Action& action) noexcept;
    void award(std::uint8_t side, std::uint64_t points) noexcept;
    void award_round(std::uint8_t side) noexcept;
    void finish_if_score() noexcept;
    void finish_on_time() noexcept;
    void finish_match(std::uint8_t side, Finish reason) noexcept;
    [[nodiscard]] std::uint8_t score_leader(bool round) const noexcept;
    [[nodiscard]] bool is_round_mode() const noexcept;
    [[nodiscard]] bool last_side_standing() noexcept;
    Rules rules_{};
    State state_{};
    std::array<Crest, kMaximumCrests> crests_{};
    std::uint64_t roundStartMs_{};
    std::uint64_t armedCharge_{};
    std::uint8_t attackingSide_{kNoSide};
};

} // namespace dawn::state::activity::crucible::match_rules
