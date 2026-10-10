#include <array>
#include <cstdio>
#include <cstdlib>

#include "state/activity/crucible/match_rules.h"

namespace rules = dawn::state::activity::crucible::match_rules;

void check(bool value, const char* reason) {
    if (!value) { std::printf("FAIL: Crucible rules: %s\n", reason); std::exit(1); }
}
rules::Rules config(rules::Mode mode) {
    rules::Rules result{};
    result.mode = mode;
    result.sideCount = 2;
    result.scoreLimit = 10;
    result.killPoints = 1;
    result.matchDurationMs = 60'000;
    return result;
}
constexpr std::array<rules::Player, 4> kTeams{{{11,0},{12,0},{21,1},{22,1}}};
constexpr std::array<rules::Player, 3> kFreeForAll{{{11,0},{21,1},{31,2}}};

void kills_and_isolation() {
    auto setup = config(rules::Mode::clash);
    setup.scoreLimit = 2;
    rules::Match first, second;
    check(first.start(100, setup, kTeams) && second.start(200, setup, kTeams),
          "independent lobby starts");
    check(first.apply({1,rules::Kill{11,12}})==rules::Status::invalid,
          "friendly kill cannot score");
    check(first.apply({1,rules::Kill{11,21}})==rules::Status::applied,
          "enemy kill scores");
    check(first.apply({1,rules::Kill{11,22}})==rules::Status::stale,
          "replayed serial rejected");
    check(first.state().sides[0].score==1 && second.state().sides[0].score==0,
          "lobby scores isolated");
    check(first.advance(1000)==rules::Status::applied
          &&second.state().nowMs==0&&first.advance(999)==rules::Status::stale,
          "lobby clocks are independent and monotonic");
    check(first.apply({2,rules::Kill{12,22}})==rules::Status::ended
          &&first.state().winner==0&&first.state().finish==rules::Finish::score,
          "score limit ends match once");
    check(first.apply({3,rules::Kill{21,11}})==rules::Status::ended,
          "finished match ignores later events");
    check(second.advance(60'000)==rules::Status::ended
          &&second.state().winner==rules::kNoSide&&second.state().finish==rules::Finish::draw,
          "scoreless time limit draws");

    auto ffa=config(rules::Mode::rumble);
    ffa.sideModel=rules::SideModel::freeForAll;
    ffa.sideCount=3;
    ffa.scoreLimit=1;
    rules::Match rumble;
    check(rumble.start(300,ffa,kFreeForAll)
          &&rumble.apply({10,rules::Kill{31,11}})==rules::Status::ended
          &&rumble.state().winner==2,"Rumble credits one player's side");
    ffa.sideModel=rules::SideModel::teams;
    check(!rumble.start(301,ffa,kFreeForAll),"Rumble cannot use team grouping");
    auto invalid=config(rules::Mode::clash);
    invalid.maximumRounds=1;
    check(!rumble.start(302,invalid,kTeams),"partial round limits rejected");
    invalid=config(rules::Mode::clash);
    check(!rumble.start(303,invalid,std::span(kTeams).first(1)),"generic rules still require opposition");
    invalid.allowUnopposed=true;
    check(rumble.start(303,invalid,std::span(kTeams).first(1)),"private mode can explicitly start alone");
    check(!rumble.start(303,invalid,{}),"private mode cannot start empty");
    invalid.mode=rules::Mode::control;invalid.zoneCount=3;
    check(!rumble.start(303,invalid,std::span(kTeams).first(1)),"unopposed override remains scoped to private kill modes");
    invalid=config(rules::Mode::clash);
    constexpr std::array<rules::Player,2> duplicates{{{11,0},{11,1}}};
    check(!rumble.start(303,invalid,duplicates),"duplicate authenticated players rejected");
}

void zone_modes() {
    auto setup=config(rules::Mode::control);
    setup.zoneCount=3;
    setup.scoreLimit=4;
    rules::Match control;
    check(control.start(400,setup,kTeams),"Control start");
    check(control.apply({1,rules::ZoneTaken{0,11}})==rules::Status::applied
          &&control.apply({2,rules::ZoneTaken{1,21}})==rules::Status::applied,
          "different sides own distinct zones");
    check(control.advance(2000)==rules::Status::applied
          &&control.state().sides[0].score==0&&control.state().sides[1].score==0,
          "zone ownership alone does not score");
    check(control.apply({3,rules::Kill{11,21}})==rules::Status::applied
          &&control.state().sides[0].score==1,"one owned zone gives one kill point");
    check(control.apply({4,rules::ZoneCleared{0,11}})==rules::Status::invalid,
          "owner cannot clear its own zone as enemy action");
    check(control.apply({4,rules::ZoneCleared{0,21}})==rules::Status::applied,
          "enemy clears zone");
    check(control.apply({5,rules::ZoneTaken{2,11}})==rules::Status::applied
          &&control.apply({6,rules::ZoneTaken{0,11}})==rules::Status::applied,
          "control side owns two zones");
    check(control.apply({7,rules::Kill{11,22}})==rules::Status::applied
          &&control.state().sides[0].score==3,"two owned zones multiply kill score");
    check(control.apply({8,rules::Kill{11,21}})==rules::Status::ended
          &&control.state().winner==0,"kill score reaches Control limit");
}

void crest_mode() {
    auto setup=config(rules::Mode::supremacy);
    setup.killPoints=0;
    setup.crestPoints=2;
    setup.crestDenyPoints=1;
    setup.scoreLimit=3;
    setup.allowUnverifiedModel=true;
    rules::Match match;
    check(match.start(500,setup,kTeams),"Supremacy start");
    check(match.apply({1,rules::Kill{11,21}})==rules::Status::applied
          &&match.state().sides[0].score==0,"kill alone creates no crest score");
    check(match.apply({2,rules::CrestDropped{99,21}})==rules::Status::applied
          &&match.state().activeCrests==1,"trusted crest drop recorded");
    check(match.apply({3,rules::CrestCollected{99,22}})==rules::Status::applied
          &&match.state().sides[1].score==1,"friendly collection denies crest");
    check(match.apply({4,rules::CrestCollected{99,11}})==rules::Status::invalid,
          "collected crest cannot score twice");
    check(match.apply({4,rules::CrestDropped{100,21}})==rules::Status::applied
          &&match.apply({5,rules::CrestCollected{100,11}})==rules::Status::applied
          &&match.state().sides[0].score==2,"enemy crest collection scores");
    check(match.apply({6,rules::CrestDropped{101,11}})==rules::Status::applied
          &&match.apply({7,rules::CrestCollected{101,12}})==rules::Status::ended
          &&match.state().winner==0,"denial points can reach explicit limit");
}

void elimination_survival_showdown() {
    auto setup=config(rules::Mode::elimination);
    setup.killPoints=0;
    setup.roundsToWin=2;
    setup.maximumRounds=3;
    setup.roundDurationMs=10'000;
    rules::Match elimination;
    check(elimination.start(600,setup,kTeams)&&elimination.begin_round(),
          "Elimination round starts");
    check(elimination.apply({1,rules::Kill{11,21}})==rules::Status::applied
          &&elimination.apply({2,rules::Revived{21}})==rules::Status::applied,
          "revive returns a fallen player");
    check(elimination.apply({3,rules::Kill{11,21}})==rules::Status::applied
          &&elimination.apply({4,rules::Kill{12,22}})==rules::Status::applied
          &&!elimination.state().roundActive&&elimination.state().sides[0].roundWins==1,
          "last side standing wins round");
    check(elimination.begin_round()
          &&elimination.apply({5,rules::RoundAwarded{0}})==rules::Status::ended
          &&elimination.state().winner==0,"second round wins match");

    setup.mode=rules::Mode::survival;
    setup.teamReserves=2;
    setup.roundsToWin=1;
    setup.maximumRounds=1;
    rules::Match survival;
    check(survival.start(601,setup,kTeams)&&survival.begin_round(),"Survival start");
    check(survival.apply({1,rules::Kill{11,21}})==rules::Status::applied
          &&survival.state().sides[1].teamReserves==1,
          "Survival death consumes a shared team reserve");
    check(survival.apply({2,rules::Kill{11,22}})==rules::Status::applied
          &&survival.state().sides[1].teamReserves==0
          &&survival.state().players[2].alive
          &&survival.apply({3,rules::Kill{12,21}})==rules::Status::applied
          &&survival.apply({4,rules::Kill{12,22}})==rules::Status::ended
          &&survival.state().winner==0,"shared reserves then remaining players exhausted");

    setup.mode=rules::Mode::showdown;
    setup.killPoints=1;
    setup.roundScoreLimit=0;
    setup.roundsToWin=1;
    setup.maximumRounds=1;
    setup.roundDurationMs=1000;
    setup.allowUnverifiedModel=true;
    rules::Match showdown;
    check(showdown.start(602,setup,kTeams)&&showdown.begin_round(),"Showdown start");
    check(showdown.apply({1,rules::Kill{21,11}})==rules::Status::applied
          &&showdown.state().sides[1].roundScore==1
          &&showdown.apply({2,rules::Revived{11}})==rules::Status::applied
          &&showdown.state().sides[1].roundScore==0,
          "Showdown revive reverses credited defeat points");
    check(showdown.apply({3,rules::Kill{21,11}})==rules::Status::applied
          &&showdown.advance(1000)==rules::Status::ended
          &&showdown.state().winner==1,"round timer selects unique score leader");
}

void objective_modes() {
    auto setup=config(rules::Mode::countdown);
    setup.killPoints=0;
    setup.roundsToWin=2;
    setup.maximumRounds=3;
    setup.roundDurationMs=10'000;
    setup.allowUnverifiedModel=true;
    rules::Match countdown;
    check(countdown.start(700,setup,kTeams)&&countdown.begin_round(0),
          "Countdown attack side explicit");
    check(countdown.apply({1,rules::ChargeArmed{77,21}})==rules::Status::invalid,
          "defender cannot arm charge");
    check(countdown.apply({1,rules::ChargeArmed{77,11}})==rules::Status::applied
          &&countdown.advance(10'000)==rules::Status::applied
          &&countdown.state().roundActive
          &&countdown.apply({2,rules::ChargeDefused{77,21}})==rules::Status::applied
          &&countdown.state().sides[1].roundWins==1,"defuse awards defense round");
    check(countdown.begin_round(1)
          &&countdown.apply({3,rules::ChargeArmed{78,21}})==rules::Status::applied
          &&countdown.apply({4,rules::ChargeDetonated{78,21}})==rules::Status::ended
          &&countdown.state().winner==1,"detonation awards attack round");

    rules::Match timeout;
    setup.roundsToWin=1;
    setup.maximumRounds=1;
    check(timeout.start(703,setup,kTeams)&&timeout.begin_round(0)
          &&timeout.advance(10'000)==rules::Status::ended
          &&timeout.state().winner==1,"unarmed charge timeout awards defense");

    rules::Match armed;
    check(armed.start(704,setup,kTeams)&&armed.begin_round(0)
          &&armed.apply({1,rules::ChargeArmed{79,11}})==rules::Status::applied
          &&armed.apply({2,rules::Kill{21,11}})==rules::Status::applied
          &&armed.apply({3,rules::Kill{22,12}})==rules::Status::applied
          &&armed.state().roundActive
          &&armed.advance(10'000)==rules::Status::applied
          &&armed.state().roundActive
          &&armed.apply({4,rules::ChargeDetonated{79,11}})==rules::Status::ended
          &&armed.state().winner==0,"armed charge persists after attacker elimination and clock expiry");

    setup.mode=rules::Mode::breakthrough;
    setup.objectivesToWin=2;
    setup.roundsToWin=1;
    setup.maximumRounds=1;
    rules::Match breakthrough;
    check(breakthrough.start(701,setup,kTeams)&&breakthrough.begin_round(0),
          "Breakthrough explicit attack side");
    check(breakthrough.apply({1,rules::ObjectiveCaptured{1,11}})==rules::Status::invalid,
          "objectives must progress in order");
    check(breakthrough.apply({1,rules::ObjectiveCaptured{0,11}})==rules::Status::applied
          &&breakthrough.apply({2,rules::ObjectiveCaptured{1,12}})==rules::Status::ended
          &&breakthrough.state().winner==0,"final objective awards attack round");

    rules::Match defense;
    check(defense.start(702,setup,kTeams)&&defense.begin_round(0)
          &&defense.apply({1,rules::ObjectiveDefended{0,21}})==rules::Status::ended
          &&defense.state().winner==1,"defended objective awards defense round");
}

int main() {
    constexpr std::array<std::string_view,19> names{{
        "pvp_control","pvp_clash","pvp_rumble","pvp_survival","pvp_countdown",
        "pvp_supremacy","pvp_breakthrough","pvp_mayhem","pvp_lockdown",
        "pvp_showdown","pvp_scorched","pvp_elimination","pvp_doubles",
        "pvp_momentum","pvp_control_iron_banner","pvp_doubles_crimson",
        "pvp_scorched_team","pvp_demolition","pvp_hardware"}};
    for (const auto name : names) {
        const auto mode=rules::find_mode(name);
        check(mode.has_value()&&rules::symbolic_name(*mode)==name,"exact symbolic identity");
    }
    check(!rules::find_mode("pvp_unknown"),"unknown mode remains unsupported");
    auto unsupported=config(rules::Mode::mayhem);
    rules::Match disabled;
    check(!disabled.start(900,unsupported,kTeams),
          "native modifiers remain disabled without an explicit model override");
    kills_and_isolation();
    zone_modes();
    crest_mode();
    elimination_survival_showdown();
    objective_modes();
    std::puts("PASS: Crucible mode identities, isolated scoring, Control, team reserves, opt-in event models and limits");
}
