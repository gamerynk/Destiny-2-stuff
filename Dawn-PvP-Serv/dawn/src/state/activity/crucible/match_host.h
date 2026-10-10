#pragma once
#include "match_rules.h"
#include <algorithm>

namespace dawn::state::activity::crucible::host {
namespace rules = match_rules;
struct Key {
    std::uint64_t owner{}, session{}, generation{};
    bool operator==(const Key&) const = default;
    bool valid() const noexcept { return owner && session && generation; }
};
struct Member {
    std::uint64_t account{}, player{};
    std::uint8_t side{};
    bool ready{};
};
enum class Phase : std::uint8_t { absent, waiting, playing, results };
struct Row {
    std::uint64_t account{}, player{};
    std::uint32_t kills{}, deaths{}, score{};
    std::uint8_t side{};
    std::array<char,64> name{};
};
struct View {
    Key key{};
    Phase phase{};
    rules::Mode mode{rules::Mode::clash};
    std::uint64_t elapsedMs{}, remainingMs{}, durationMs{}, endedAt{};
    std::uint32_t scoreLimit{}, revision{};
    std::uint8_t count{}, winner{rules::kNoSide};
    std::array<Row,12> rows{};
    std::array<std::uint32_t,12> scores{};
};
class Match {
public:
    bool configure(Key key, rules::Mode mode, std::uint32_t limit=0,
                   std::uint64_t duration=600'000) noexcept {
        if (!key.valid() || (mode!=rules::Mode::clash && mode!=rules::Mode::rumble)
            || !duration || duration>86'400'000 || limit>1'000'000) return false;
        *this={}; view_.key=key; view_.phase=Phase::waiting; view_.mode=mode;
        view_.scoreLimit=limit?limit:(mode==rules::Mode::clash?100:20);
        view_.durationMs=duration; view_.remainingMs=duration; view_.revision=1;
        return true;
    }
    bool roster(std::span<const Member> members, std::size_t expected, std::uint64_t now) noexcept {
        if (view_.phase==Phase::absent || view_.phase==Phase::results || members.size()>12) return false;
        connected_={};
        std::array<bool,12> sides{};
        bool allReady=!members.empty() && members.size()>=expected;
        for (std::size_t i=0;i<members.size();++i) {
            const auto& m=members[i];
            if (!m.account || !m.player || m.side>=(view_.mode==rules::Mode::clash?2:12)) return false;
            for(std::size_t j=0;j<i;++j)
                if(m.account==members[j].account || m.player==members[j].player) return false;
            if(view_.mode==rules::Mode::rumble && sides[m.side]) return false;
            sides[m.side]=true; allReady&=m.ready;
            const auto existing=find(m.account);
            if(view_.phase==Phase::playing && (existing==12 || view_.rows[existing].player!=m.player
                || view_.rows[existing].side!=m.side)) return false;
        }
        // original participants stay in final results after disconnecting; native join-in-progress
        // is closed once this fixed-roster match starts
        if(view_.phase==Phase::playing) {
            for(const auto& m:members) connected_[find(m.account)]=m.ready;
            tick(now); return true;
        }
        view_.count=static_cast<std::uint8_t>(members.size());
        for(std::size_t i=0;i<members.size();++i)
            view_.rows[i]={members[i].account,members[i].player,0,0,0,members[i].side,{}};
        if(!allReady) return true;
        rules::Rules r{};r.mode=view_.mode;r.sideCount=view_.mode==rules::Mode::clash?2:12;
        r.sideModel=view_.mode==rules::Mode::clash?rules::SideModel::teams:rules::SideModel::freeForAll;
        r.scoreLimit=view_.scoreLimit;r.killPoints=1;r.matchDurationMs=view_.durationMs;
        r.allowUnopposed=true;
        std::array<rules::Player,12> players{};
        for(std::size_t i=0;i<members.size();++i) { players[i]={members[i].account,members[i].side}; connected_[i]=true; }
        if(!rules_.start(view_.key.session,r,std::span(players).first(members.size()))) return false;
        start_=now;view_.phase=Phase::playing;++view_.revision;return true;
    }
    void tick(std::uint64_t now) noexcept {
        if(view_.phase!=Phase::playing || now<start_) return;
        const auto elapsed=(std::min)(now-start_,view_.durationMs);
        (void)rules_.advance(elapsed);sync(now);
    }
    bool kill(std::uint64_t reporter, std::uint64_t victimPlayer, std::uint32_t sequence,
              std::uint64_t fingerprint, std::uint64_t now) noexcept {
        if(now<start_ || now-start_<view_.elapsedMs) return false;
        tick(now);
        if(view_.phase!=Phase::playing) return false;
        const auto killer=find(reporter);
        std::size_t victim=12;
        for(std::size_t i=0;i<view_.count;++i) if(view_.rows[i].player==victimPlayer) victim=i;
        if(killer==12 || victim==12 || killer==victim || !connected_[killer] || !connected_[victim]
            || view_.rows[killer].side==view_.rows[victim].side) return false;
        // native v38 often sends sequence zero; suppress a byte-identical report for one second
        // rather than suppressing every later kill of this player
        for(const auto& r:receipts_) if(r.reporter==reporter && r.victim==victimPlayer
            && (sequence ? r.sequence==sequence : r.sequence==0 && r.fingerprint==fingerprint
                && now>=r.when && now-r.when<1'000)) return false;
        const auto status=rules_.apply({++serial_,rules::Kill{reporter,view_.rows[victim].account}});
        if(status!=rules::Status::applied && status!=rules::Status::ended) return false;
        receipts_[receiptNext_++%receipts_.size()]={reporter,victimPlayer,fingerprint,now,sequence};
        ++view_.rows[killer].kills;++view_.rows[victim].deaths;++view_.rows[killer].score;
        ++view_.revision;sync(now);return true;
    }
    View view() const noexcept { return view_; }
private:
    std::size_t find(std::uint64_t account) const noexcept {
        for(std::size_t i=0;i<view_.count;++i) if(view_.rows[i].account==account) return i;
        return 12;
    }
    void sync(std::uint64_t now) noexcept {
        const auto state=rules_.state();view_.elapsedMs=state.nowMs;
        view_.remainingMs=view_.durationMs-(std::min)(view_.durationMs,state.nowMs);
        for(std::size_t i=0;i<12;++i) view_.scores[i]=state.sides[i].score;
        if(state.finish!=rules::Finish::none) {
            view_.phase=Phase::results;view_.winner=state.winner;view_.endedAt=now;++view_.revision;
        }
    }
    struct Receipt { std::uint64_t reporter{},victim{},fingerprint{},when{}; std::uint32_t sequence{}; };
    View view_{};rules::Match rules_{};std::uint64_t start_{},serial_{};
    std::array<bool,12> connected_{};
    std::array<Receipt,128> receipts_{};std::size_t receiptNext_{};
};
}
