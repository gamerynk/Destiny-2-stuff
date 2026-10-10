#include "match_rules.h"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace dawn::state::activity::crucible::match_rules {
namespace {

struct ModeRow final { Mode mode; std::string_view name; Family family; };
constexpr std::array<ModeRow, 19> kModes{{
    {Mode::control, "pvp_control", Family::zones},
    {Mode::clash, "pvp_clash", Family::kills},
    {Mode::rumble, "pvp_rumble", Family::kills},
    {Mode::survival, "pvp_survival", Family::survival},
    {Mode::countdown, "pvp_countdown", Family::charge},
    {Mode::supremacy, "pvp_supremacy", Family::crests},
    {Mode::breakthrough, "pvp_breakthrough", Family::breakthrough},
    {Mode::mayhem, "pvp_mayhem", Family::kills},
    {Mode::lockdown, "pvp_lockdown", Family::zones},
    {Mode::showdown, "pvp_showdown", Family::showdown},
    {Mode::scorched, "pvp_scorched", Family::kills},
    {Mode::elimination, "pvp_elimination", Family::elimination},
    {Mode::doubles, "pvp_doubles", Family::kills},
    {Mode::momentum, "pvp_momentum", Family::zones},
    {Mode::controlIronBanner, "pvp_control_iron_banner", Family::zones},
    {Mode::doublesCrimson, "pvp_doubles_crimson", Family::kills},
    {Mode::scorchedTeam, "pvp_scorched_team", Family::kills},
    {Mode::demolition, "pvp_demolition", Family::charge},
    {Mode::hardware, "pvp_hardware", Family::kills},
}};
constexpr std::uint64_t kMaximumDurationMs = 24ULL * 60 * 60 * 1000;
constexpr std::uint32_t kMaximumAward = 1'000'000;

[[nodiscard]] bool round_family(Family family, Mode mode) noexcept {
    return family == Family::elimination || family == Family::survival
           || family == Family::charge || family == Family::showdown
           || family == Family::breakthrough || mode == Mode::lockdown;
}
[[nodiscard]] bool established_subset(Mode mode) noexcept {
    return mode == Mode::clash || mode == Mode::rumble || mode == Mode::control
           || mode == Mode::survival || mode == Mode::elimination;
}
[[nodiscard]] bool valid_rules(const Rules& rules) noexcept {
    if (symbolic_name(rules.mode).empty() || rules.sideCount < 2
        || rules.sideCount > kMaximumSides || rules.zoneCount > kMaximumZones
        || rules.matchDurationMs > kMaximumDurationMs
        || rules.roundDurationMs > kMaximumDurationMs
        || rules.killPoints > kMaximumAward || rules.crestPoints > kMaximumAward
        || rules.crestDenyPoints > kMaximumAward
        || (!established_subset(rules.mode) && !rules.allowUnverifiedModel)) {
        return false;
    }
    const Family family = family_for(rules.mode);
    const bool rounds = round_family(family, rules.mode);
    if (rules.allowUnopposed && rules.mode != Mode::clash && rules.mode != Mode::rumble)
        return false;
    if (rules.mode == Mode::rumble && rules.sideModel != SideModel::freeForAll) return false;
    if (rules.mode == Mode::clash && rules.sideModel != SideModel::teams) return false;
    if (family != Family::kills && rules.sideModel != SideModel::teams) return false;
    if (rounds) {
        if (rules.roundsToWin == 0 || rules.maximumRounds == 0
            || rules.roundsToWin > rules.maximumRounds || rules.roundDurationMs == 0)
            return false;
    } else if (rules.roundsToWin != 0 || rules.maximumRounds != 0
               || rules.roundDurationMs != 0 || rules.roundScoreLimit != 0) return false;
    if (!rounds && rules.scoreLimit == 0 && rules.matchDurationMs == 0) return false;
    if (family == Family::kills && rules.killPoints == 0) return false;
    if (family == Family::zones && (rules.zoneCount == 0 || rules.killPoints == 0)) return false;
    if (family == Family::crests && rules.crestPoints == 0) return false;
    if (family == Family::survival && rules.teamReserves == 0) return false;
    // a defeat can still be reversed by a revive, so a score limit would close the round too early
    if (family == Family::showdown && rules.roundScoreLimit != 0) return false;
    if (family == Family::breakthrough && rules.objectivesToWin == 0) return false;
    if ((family == Family::charge || family == Family::breakthrough)
        && rules.sideCount != 2) return false;
    return true;
}
[[nodiscard]] std::uint32_t add_capped(std::uint32_t current, std::uint64_t amount) noexcept {
    const auto ceiling = (std::numeric_limits<std::uint32_t>::max)();
    return amount >= ceiling - current ? ceiling : current + static_cast<std::uint32_t>(amount);
}
} // namespace

std::optional<Mode> find_mode(std::string_view symbolicName) noexcept {
    for (const auto& row : kModes) if (row.name == symbolicName) return row.mode;
    return std::nullopt;
}
std::string_view symbolic_name(Mode mode) noexcept {
    for (const auto& row : kModes) if (row.mode == mode) return row.name;
    return {};
}
Family family_for(Mode mode) noexcept {
    for (const auto& row : kModes) if (row.mode == mode) return row.family;
    return Family::kills;
}

bool Match::start(std::uint64_t lobbyId, Rules rules, std::span<const Player> roster) noexcept {
    if (lobbyId == 0 || !valid_rules(rules) || roster.empty()
        || (!rules.allowUnopposed && roster.size() < 2)
        || roster.size() > kMaximumPlayers) return false;
    std::array<std::uint8_t, kMaximumSides> members{};
    for (std::size_t index = 0; index < roster.size(); ++index) {
        const Player value = roster[index];
        if (value.id == 0 || value.side >= rules.sideCount) return false;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (roster[prior].id == value.id) return false;
        }
        ++members[value.side];
        if (rules.sideModel == SideModel::freeForAll && members[value.side] > 1) return false;
    }
    if (!rules.allowUnopposed && std::count_if(members.begin(), members.begin() + rules.sideCount,
                      [](std::uint8_t count) { return count != 0; }) < 2) return false;
    Match next{};
    next.rules_ = rules;
    next.state_.lobbyId = lobbyId;
    next.state_.active = true;
    next.state_.sideCount = rules.sideCount;
    next.state_.playerCount = static_cast<std::uint8_t>(roster.size());
    next.state_.zoneOwners.fill(kNoSide);
    for (std::size_t index = 0; index < roster.size(); ++index) {
        next.state_.players[index] = {roster[index], true, kNoSide, 0};
    }
    if (!round_family(family_for(rules.mode), rules.mode)) next.state_.roundActive = true;
    *this = next;
    return true;
}

bool Match::is_round_mode() const noexcept {
    return round_family(family_for(rules_.mode), rules_.mode);
}

bool Match::begin_round(std::uint8_t attackingSide) noexcept {
    if (!state_.active || state_.finish != Finish::none || !is_round_mode()
        || state_.roundActive || state_.roundsStarted >= rules_.maximumRounds) return false;
    const Family family = family_for(rules_.mode);
    if (family == Family::charge || family == Family::breakthrough) {
        if (attackingSide >= rules_.sideCount) return false;
    } else if (attackingSide != kNoSide) return false;
    attackingSide_ = attackingSide;
    roundStartMs_ = state_.nowMs;
    armedCharge_ = 0;
    state_.objectiveProgress = 0;
    state_.zoneOwners.fill(kNoSide);
    state_.roundActive = true;
    ++state_.roundsStarted;
    for (std::size_t side = 0; side < rules_.sideCount; ++side) {
        state_.sides[side].roundScore = 0;
        state_.sides[side].teamReserves = rules_.teamReserves;
    }
    for (std::size_t index = 0; index < state_.playerCount; ++index) {
        state_.players[index].alive = true;
        state_.players[index].lastDefeatSide = kNoSide;
        state_.players[index].lastDefeatPoints = 0;
    }
    for (auto& crest : crests_) crest = {};
    state_.activeCrests = 0;
    return true;
}

PlayerState* Match::player(std::uint64_t id) noexcept {
    for (std::size_t index = 0; index < state_.playerCount; ++index)
        if (state_.players[index].player.id == id) return &state_.players[index];
    return nullptr;
}
const PlayerState* Match::player(std::uint64_t id) const noexcept {
    for (std::size_t index = 0; index < state_.playerCount; ++index)
        if (state_.players[index].player.id == id) return &state_.players[index];
    return nullptr;
}

void Match::finish_match(std::uint8_t side, Finish reason) noexcept {
    state_.winner = side;
    state_.finish = side == kNoSide ? Finish::draw : reason;
    state_.roundActive = false;
    state_.active = false;
}
std::uint8_t Match::score_leader(bool round) const noexcept {
    std::uint32_t top = 0;
    std::uint8_t leader = kNoSide;
    bool tied = false;
    for (std::uint8_t side = 0; side < rules_.sideCount; ++side) {
        const std::uint32_t score = round ? state_.sides[side].roundScore
                                          : state_.sides[side].score;
        if (score > top) { top = score; leader = side; tied = false; }
        else if (score != 0 && score == top) tied = true;
    }
    return tied ? kNoSide : leader;
}
void Match::finish_if_score() noexcept {
    if (state_.finish != Finish::none) return;
    if (is_round_mode()) {
        if (rules_.roundScoreLimit != 0 && state_.roundActive) {
            const auto leader = score_leader(true);
            if (leader != kNoSide
                && state_.sides[leader].roundScore >= rules_.roundScoreLimit) award_round(leader);
        }
        return;
    }
    if (rules_.scoreLimit == 0) return;
    const auto leader = score_leader(false);
    if (leader != kNoSide && state_.sides[leader].score >= rules_.scoreLimit)
        finish_match(leader, Finish::score);
}
void Match::award(std::uint8_t side, std::uint64_t points) noexcept {
    if (side >= rules_.sideCount || points == 0 || state_.finish != Finish::none) return;
    state_.sides[side].score = add_capped(state_.sides[side].score, points);
    if (is_round_mode())
        state_.sides[side].roundScore = add_capped(state_.sides[side].roundScore, points);
    finish_if_score();
}
void Match::award_round(std::uint8_t side) noexcept {
    if (side >= rules_.sideCount || !state_.roundActive) return;
    state_.roundActive = false;
    ++state_.sides[side].roundWins;
    if (state_.sides[side].roundWins >= rules_.roundsToWin) {
        finish_match(side, Finish::rounds);
    } else if (state_.roundsStarted >= rules_.maximumRounds) {
        std::uint16_t top = 0;
        std::uint8_t leader = kNoSide;
        bool tied = false;
        for (std::uint8_t row = 0; row < rules_.sideCount; ++row) {
            const auto wins = state_.sides[row].roundWins;
            if (wins > top) { top = wins; leader = row; tied = false; }
            else if (wins != 0 && wins == top) tied = true;
        }
        finish_match(tied ? kNoSide : leader, Finish::rounds);
    }
}
bool Match::last_side_standing() noexcept {
    std::uint8_t survivor = kNoSide;
    for (std::size_t index = 0; index < state_.playerCount; ++index) {
        const auto& row = state_.players[index];
        if (!row.alive) continue;
        if (survivor != kNoSide && survivor != row.player.side) return false;
        survivor = row.player.side;
    }
    if (survivor == kNoSide) {
        state_.roundActive = false;
        if (state_.roundsStarted >= rules_.maximumRounds) finish_match(kNoSide, Finish::draw);
    } else if (family_for(rules_.mode) == Family::charge && armedCharge_ != 0
               && survivor != attackingSide_) {
        return false;
    } else award_round(survivor);
    return true;
}
void Match::finish_on_time() noexcept {
    if (is_round_mode()) {
        std::uint16_t top = 0;
        std::uint8_t leader = kNoSide;
        bool tied = false;
        for (std::uint8_t side = 0; side < rules_.sideCount; ++side) {
            const auto wins = state_.sides[side].roundWins;
            if (wins > top) { top = wins; leader = side; tied = false; }
            else if (wins != 0 && wins == top) tied = true;
        }
        finish_match(tied ? kNoSide : leader, Finish::time);
    } else finish_match(score_leader(false), Finish::time);
}

Status Match::advance(std::uint64_t elapsedMs) noexcept {
    if (state_.finish != Finish::none) return Status::ended;
    if (!state_.active) return Status::inactive;
    if (elapsedMs < state_.nowMs) return Status::stale;
    if (elapsedMs > kMaximumDurationMs) return Status::invalid;
    state_.nowMs = elapsedMs;
    const std::uint64_t roundEnd = roundStartMs_ + rules_.roundDurationMs;
    if (is_round_mode() && state_.roundActive && elapsedMs >= roundEnd) {
        if (family_for(rules_.mode) == Family::charge) {
            if (armedCharge_ == 0) award_round(static_cast<std::uint8_t>(1 - attackingSide_));
        } else {
            const auto leader = score_leader(true);
            if (leader != kNoSide) award_round(leader);
            else {
                state_.roundActive = false;
                if (state_.roundsStarted >= rules_.maximumRounds) finish_on_time();
            }
        }
    }
    if (rules_.matchDurationMs != 0 && elapsedMs >= rules_.matchDurationMs
        && state_.finish == Finish::none
        && !(family_for(rules_.mode) == Family::charge
             && state_.roundActive && armedCharge_ != 0)) finish_on_time();
    return state_.finish == Finish::none ? Status::applied : Status::ended;
}

Status Match::apply_action(const Action& action) noexcept {
    const Family family = family_for(rules_.mode);
    return std::visit([this, family](const auto& value) noexcept -> Status {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Kill>) {
            auto* killer = player(value.killer);
            auto* victim = player(value.victim);
            if (killer == nullptr || victim == nullptr || killer == victim
                || killer->player.side == victim->player.side
                || !killer->alive || !victim->alive) return Status::invalid;
            if (family == Family::elimination || family == Family::charge
                || family == Family::showdown) victim->alive = false;
            if (family == Family::survival) {
                auto& reserves = state_.sides[victim->player.side].teamReserves;
                if (reserves != 0) --reserves;
                else victim->alive = false;
            }
            std::uint64_t points = rules_.killPoints;
            if (family == Family::zones) {
                std::uint8_t owned = 0;
                for (std::uint8_t zone = 0; zone < rules_.zoneCount; ++zone)
                    owned += state_.zoneOwners[zone] == killer->player.side;
                points *= (std::max)(std::uint8_t{1}, owned);
            }
            if (family == Family::showdown) {
                victim->lastDefeatSide = killer->player.side;
                victim->lastDefeatPoints = static_cast<std::uint32_t>(points);
            }
            award(killer->player.side, points);
            if ((family == Family::elimination || family == Family::survival
                 || family == Family::charge)
                && state_.roundActive) (void)last_side_standing();
            return Status::applied;
        } else if constexpr (std::is_same_v<T, ZoneTaken>) {
            const auto* actor = player(value.actor);
            if (family != Family::zones || actor == nullptr || value.zone >= rules_.zoneCount
                || state_.zoneOwners[value.zone] == actor->player.side) return Status::invalid;
            state_.zoneOwners[value.zone] = actor->player.side;
            return Status::applied;
        } else if constexpr (std::is_same_v<T, ZoneCleared>) {
            const auto* actor = player(value.actor);
            if (family != Family::zones || actor == nullptr || value.zone >= rules_.zoneCount
                || state_.zoneOwners[value.zone] == kNoSide
                || state_.zoneOwners[value.zone] == actor->player.side) return Status::invalid;
            state_.zoneOwners[value.zone] = kNoSide;
            return Status::applied;
        } else if constexpr (std::is_same_v<T, CrestDropped>) {
            const auto* victim = player(value.victim);
            if (family != Family::crests || victim == nullptr || value.crest == 0) return Status::invalid;
            Crest* vacant = nullptr;
            for (auto& crest : crests_) {
                if (crest.active && crest.id == value.crest) return Status::invalid;
                if (!crest.active && vacant == nullptr) vacant = &crest;
            }
            if (vacant == nullptr) return Status::invalid;
            *vacant = {value.crest, victim->player.side, true};
            ++state_.activeCrests;
            return Status::applied;
        } else if constexpr (std::is_same_v<T, CrestCollected>) {
            const auto* collector = player(value.collector);
            if (family != Family::crests || collector == nullptr) return Status::invalid;
            for (auto& crest : crests_) {
                if (!crest.active || crest.id != value.crest) continue;
                const auto points = collector->player.side == crest.victimSide
                    ? rules_.crestDenyPoints : rules_.crestPoints;
                crest.active = false;
                --state_.activeCrests;
                award(collector->player.side, points);
                return Status::applied;
            }
            return Status::invalid;
        } else if constexpr (std::is_same_v<T, CrestExpired>) {
            if (family != Family::crests) return Status::invalid;
            for (auto& crest : crests_) {
                if (!crest.active || crest.id != value.crest) continue;
                crest.active = false;
                --state_.activeCrests;
                return Status::applied;
            }
            return Status::invalid;
        } else if constexpr (std::is_same_v<T, Revived>) {
            auto* target = player(value.player);
            if ((family != Family::elimination && family != Family::showdown)
                || target == nullptr || target->alive) return Status::invalid;
            if (family == Family::showdown && target->lastDefeatSide < rules_.sideCount) {
                auto& scored = state_.sides[target->lastDefeatSide];
                scored.score -= (std::min)(scored.score, target->lastDefeatPoints);
                scored.roundScore -= (std::min)(scored.roundScore, target->lastDefeatPoints);
                target->lastDefeatSide = kNoSide;
                target->lastDefeatPoints = 0;
            }
            target->alive = true;
            return Status::applied;
        } else if constexpr (std::is_same_v<T, ChargeArmed>) {
            const auto* actor = player(value.actor);
            if (family != Family::charge || actor == nullptr || value.charge == 0
                || armedCharge_ != 0 || actor->player.side != attackingSide_) return Status::invalid;
            armedCharge_ = value.charge;
            return Status::applied;
        } else if constexpr (std::is_same_v<T, ChargeDefused>) {
            const auto* actor = player(value.actor);
            if (family != Family::charge || actor == nullptr || value.charge == 0
                || armedCharge_ != value.charge || actor->player.side == attackingSide_) return Status::invalid;
            armedCharge_ = 0;
            award_round(actor->player.side);
            return Status::applied;
        } else if constexpr (std::is_same_v<T, ChargeDetonated>) {
            const auto* actor = player(value.actor);
            if (family != Family::charge || actor == nullptr || value.charge == 0
                || armedCharge_ != value.charge || actor->player.side != attackingSide_) return Status::invalid;
            armedCharge_ = 0;
            award_round(actor->player.side);
            return Status::applied;
        } else if constexpr (std::is_same_v<T, ObjectiveCaptured>) {
            const auto* actor = player(value.actor);
            if (family != Family::breakthrough || actor == nullptr
                || actor->player.side != attackingSide_
                || value.objective != state_.objectiveProgress) return Status::invalid;
            ++state_.objectiveProgress;
            if (state_.objectiveProgress >= rules_.objectivesToWin) award_round(attackingSide_);
            return Status::applied;
        } else if constexpr (std::is_same_v<T, ObjectiveDefended>) {
            const auto* actor = player(value.actor);
            if (family != Family::breakthrough || actor == nullptr
                || actor->player.side == attackingSide_
                || value.objective != state_.objectiveProgress) return Status::invalid;
            award_round(actor->player.side);
            return Status::applied;
        } else if constexpr (std::is_same_v<T, RoundAwarded>) {
            if (!is_round_mode() || value.side >= rules_.sideCount) return Status::invalid;
            bool hasPlayer = false;
            for (std::size_t index = 0; index < state_.playerCount; ++index)
                hasPlayer |= state_.players[index].player.side == value.side;
            if (!hasPlayer) return Status::invalid;
            award_round(value.side);
            return Status::applied;
        }
    }, action);
}

Status Match::apply(const Event& event) noexcept {
    if (state_.finish != Finish::none) return Status::ended;
    if (!state_.active || !state_.roundActive) return Status::inactive;
    if (event.serial == 0 || event.serial <= state_.lastSerial) return Status::stale;
    const Status result = apply_action(event.action);
    if (result != Status::applied) return result;
    state_.lastSerial = event.serial;
    return state_.finish == Finish::none ? Status::applied : Status::ended;
}
State Match::state() const noexcept { return state_; }

} // namespace dawn::state::activity::crucible::match_rules
