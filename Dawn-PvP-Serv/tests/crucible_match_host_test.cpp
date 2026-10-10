#include <cstdio>
#include <cstdlib>
#include "state/activity/crucible/match_host.h"
#include "middleware/crucible/match_view_wire.h"
namespace host=dawn::state::activity::crucible::host;
namespace rules=dawn::state::activity::crucible::match_rules;
namespace wire=dawn::middleware::crucible::match_view_wire;
void check(bool b,const char* s) { if(!b) {std::printf("FAIL: %s\n",s);std::exit(1);} }
constexpr std::array<host::Member,4> team{{{1,101,0,true},{2,102,0,true},{3,103,1,true},{4,104,1,true}}};
int main() {
    for (const auto mode:{rules::Mode::clash,rules::Mode::rumble}) {
        host::Match practice;
        check(practice.configure({4,14,1},mode,25,480'000),"configure solo menu selections");
        check(practice.roster({},0,100) && practice.view().phase==host::Phase::waiting,"empty lobby stays waiting");
        auto member=std::array<host::Member,1>{{{1,101,0,false}}};
        check(practice.roster(member,1,200) && practice.view().phase==host::Phase::waiting,"solo waits for world ready");
        member[0].ready=true;
        check(practice.roster(member,2,300) && practice.view().phase==host::Phase::waiting,"solo waits for reserved peer");
        check(practice.roster(member,1,1000) && practice.view().phase==host::Phase::playing,"solo private clock starts");
        practice.tick(2000);
        check(practice.view().remainingMs==479'000 && practice.view().scoreLimit==25,"solo clock uses selected duration and score");
        check(practice.roster(member,1,3000) && practice.view().remainingMs==478'000,"solo publications cannot reset clock");
        check(!practice.kill(1,101,1,1,3100),"solo cannot score a self kill");
        practice.tick(480'999);check(practice.view().phase==host::Phase::playing,"solo timer does not end early");
        practice.tick(481'000);check(practice.view().phase==host::Phase::results && practice.view().winner==255,"solo time limit completes as draw");
    }
    host::Match a,b;check(a.configure({1,10,1},rules::Mode::clash,2,10'000),"configure clash");
    check(b.configure({1,11,1},rules::Mode::clash,2,10'000),"configure parallel lobby");
    check(a.roster(std::span(team).first(2),4,100) && a.view().phase==host::Phase::waiting,"wait for full party");
    auto loading=team;loading[3].ready=false;
    check(a.roster(loading,4,500) && a.view().phase==host::Phase::waiting,"wait for arrival");
    check(a.roster(team,4,1000) && b.roster(team,4,1000),"two independent matches");
    check(!a.kill(1,102,1,1,1100) && !a.kill(7,103,1,1,1100)
        && !a.kill(1,999,1,1,1100),"friendly, foreign reporter and unknown victim refused");
    check(a.kill(1,103,0,77,1200),"first kill");
    check(!a.kill(1,103,0,77,1250),"native zero-sequence duplicate refused");
    check(a.view().scores[0]==1 && b.view().scores[0]==0,"lobby score isolation");
    check(!a.kill(1,104,2,78,1100),"backward clock refused");
    check(a.kill(2,104,2,78,1500) && a.view().phase==host::Phase::results
        && a.view().winner==0 && a.view().scores[0]==2,"score limit completes with winner");
    const auto final=a.view();a.tick(90'000);
    check(!a.kill(3,101,3,99,2000) && a.view().revision==final.revision
        && a.view().scores==final.scores,"result freezes, late kill refused");
    b.tick(11'000);check(b.view().phase==host::Phase::results && b.view().winner==255,"timer draw");
    host::Match ffa;auto solo=team;solo[0].side=0;solo[1].side=2;solo[2].side=5;solo[3].side=8;
    check(ffa.configure({2,12,1},rules::Mode::rumble,20,10000) && ffa.roster(solo,4,1000),"noncontiguous native FFA assignments");
    check(ffa.kill(3,101,5,55,1100) && !ffa.kill(3,101,5,56,3000),"nonzero receipt replay refused");
    auto departed=std::span(solo).first(3);
    check(ffa.roster(departed,4,3500) && !ffa.kill(4,101,6,66,3600),"departed sender refused");
    ffa.tick(11000);check(ffa.view().winner==5 && ffa.view().rows[2].kills==1
        && ffa.view().rows[0].deaths==1,"FFA time winner and final counters");
    host::Match bad;check(!bad.configure({3,13,1},rules::Mode::control),"objective modes excluded");
    check(bad.configure({3,13,1},rules::Mode::rumble),"new rumble");
    check(!bad.roster(team,4,1000),"duplicate FFA sides refused");
    std::array<std::byte,wire::kMaximum> bytes{};std::size_t n{};
    check(wire::encode(ffa.view(),bytes,n),"result encode");host::View decoded{};
    check(wire::decode(std::span(bytes).first(n),decoded) && decoded.key==ffa.view().key
        && decoded.rows[2].kills==1 && decoded.winner==5,"result wire roundtrip");
    for(std::size_t i=0;i<n;++i) check(!wire::decode(std::span(bytes).first(i),decoded),"truncated result refused");
    check(!wire::decode(std::span(bytes).first(n+1),decoded),"trailing result refused");
    check(!wire::encode(ffa.view(),std::span(bytes).first(10),n) && n==0,"short output refused");
    puts("PASS: Clash/Rumble start, kills, replay, clock, concurrent lobbies, departures, results and wire bounds");
}
