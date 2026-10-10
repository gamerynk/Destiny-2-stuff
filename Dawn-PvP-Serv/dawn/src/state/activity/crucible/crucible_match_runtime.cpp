#include "match_runtime.h"
#include <Windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "native_modes.h"
#include "native_maps.h"
#include "native_death_receipt.h"
#include "../runtime.h"
#include "../shared/runtime.h"
#include "../../runtime/runtime.h"
#include "../../../middleware/bap/activity_message/activity_player_assignment.h"
#include "../../../middleware/bap/activity_message/incident.h"
#include "../../../middleware/encoding/bit_reader.h"
#include "../../../core/logging/log.h"

namespace dawn::state::activity::crucible::runtime {
namespace {
namespace rules=match_rules;
struct Entry {
    host::Match match{};
    std::array<std::uint64_t,12> readyAccounts{}, readyTicks{};
    std::uint64_t used{}, created{};
    bool archived{};
};
SRWLOCK lock=SRWLOCK_INIT;
std::array<Entry,64> matches{};
struct Input {
    host::Key key{};rules::Mode mode{};std::uint64_t account{};
    menu_rules::Rules selectedRules{};
    std::array<host::Member,12> members{};std::size_t count{},expected{};
};
bool gather(std::uint64_t session,Input& in) noexcept {
    in={};
    std::uint64_t account{};
    const auto self=capture_account_runtime(account);
    SessionRecord local{};
    if(!snapshot_for_runtime(self,account,session,local) || !local.joined
        || !local.membership.hasIdentity || local.publicWorld) return false;
    const auto* mode=find_native_mode(local.destination.previousActivityIndex);
    if(!mode || (mode->symbolicName!="pvp_clash" && mode->symbolicName!="pvp_rumble")) return false;
    const std::string_view map{reinterpret_cast<const char*>(local.destination.packageName.data()),
        local.destination.packageNameLength};
    if(!find_native_map(map)) return false;
    AccountRuntimeContext owner{};SessionRecord root{};
    if(!shared::canonical_context(session,owner) || !owner.account
        || !snapshot_for_runtime(owner,local.sharedOwnerAccountSoid,session,root)) return false;
    std::array<std::uint64_t,32> accounts{};
    if(!shared::participant_accounts_snapshot(root,accounts)) return false;
    in.key={root.ownerAccountSoid,session,root.createdRevision};in.account=account;
    in.mode=mode->symbolicName=="pvp_clash"?rules::Mode::clash:rules::Mode::rumble;
    if (root.destination.menuOptionCount>root.destination.menuOptions.size()
        || !menu_rules::resolve(std::span(root.destination.menuOptions).first(root.destination.menuOptionCount),
                                in.selectedRules)) return false;
    for(auto id:accounts) if(id) {
        SessionRecord peer{};
        if(in.count==12 || !shared::participant_snapshot(root,id,peer)) return false;
        namespace assignment=middleware::bap::activity_message::player_assignment;
        const auto team=assignment::resolve(true,assignment::mode_for(mode->symbolicName),
            peer.membership.peerConnection.playerTeam,peer.sharedMemberSlot);
        if(!team.publishRequestedTeam) return false;
        const auto side=team.teamIndex;
        // type-23 joinIdentity is a random session lookup token; native death receipts and
        // scoreboard rows use field5, the admitted character SOID
        in.members[in.count++]={id,peer.membership.identity.opaqueSoid,side,
            peer.membership.region.index>=0};
    }
    in.expected=in.count;
    for(const auto& reserved:root.membership.reservations) {
        if(reserved.accountSoid && reserved.accountSoid!=root.ownerAccountSoid
            && std::find(accounts.begin(),accounts.end(),reserved.accountSoid)==accounts.end()) ++in.expected;
    }
    return in.count>0 && in.expected<=12;
}
bool archive(const host::View& v) noexcept {
    try {
        std::array<wchar_t,32768> path{};
        const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
        if(!length || length>=path.size()) return false;
        auto folder=std::filesystem::path(path.data()).parent_path()/L"Restoration"/L"matches";
        std::filesystem::create_directories(folder);
        const auto filename=std::to_string(v.key.owner)+"-"+std::to_string(v.key.session)+"-"
            +std::to_string(v.key.generation)+"-"+std::to_string(v.endedAt)+".json";
        const auto dest=folder/filename;auto temp=dest;temp+=L".tmp";
        std::ofstream file(temp,std::ios::binary|std::ios::trunc);
        file<<"{\"version\":1,\"session\":"<<v.key.session<<",\"mode\":\""
            <<(v.mode==rules::Mode::clash?"clash":"rumble")<<"\",\"elapsed_ms\":"<<v.elapsedMs
            <<",\"score_limit\":"<<v.scoreLimit<<",\"winner\":"<<unsigned(v.winner)<<",\"players\":[";
        for(std::size_t i=0;i<v.count;++i) {
            const auto& r=v.rows[i];if(i) file<<',';
            file<<"{\"account\":"<<r.account<<",\"team\":"<<unsigned(r.side)<<",\"kills\":"
                <<r.kills<<",\"deaths\":"<<r.deaths<<",\"score\":"<<r.score<<'}';
        }
        file<<"]}\n";file.close();if(!file) return false;
        std::filesystem::rename(temp,dest);return true;
    } catch(...) { return false; }
}
Entry* find(const host::Key& key,std::uint64_t now) noexcept {
    for(auto& entry:matches) if(entry.match.view().key==key) return &entry;
    for(auto& entry:matches) if(entry.match.view().phase==host::Phase::absent
        || (now>=entry.used && now-entry.used>30*60*1000)) { entry={};return &entry; }
    return nullptr;
}
bool update(const Input& in,bool ready,std::uint64_t now,host::View& out) noexcept {
    auto* entry=find(in.key,now);if(!entry) return false;
    if(entry->match.view().phase==host::Phase::absent) {
        if(!entry->match.configure(in.key,in.mode,in.selectedRules.scoreLimit,in.selectedRules.durationMs)) return false;
        entry->created=now;
    }
    if(entry->match.view().mode!=in.mode) return false;
    entry->used=now;
    for(std::size_t i=0;i<12;++i) if(entry->readyAccounts[i]==in.account || !entry->readyAccounts[i]) {
        entry->readyAccounts[i]=in.account;entry->readyTicks[i]=ready?now:0;break;
    }
    auto members=in.members;
    for(std::size_t i=0;i<in.count;++i) {
        bool reported=false;
        for(std::size_t j=0;j<12;++j) if(entry->readyAccounts[j]==members[i].account)
            reported=entry->readyTicks[j]!=0 && now>=entry->readyTicks[j] && now-entry->readyTicks[j]<20'500;
        members[i].ready&=reported;
    }
    const auto prior=entry->match.view().phase;
    const bool rosterAccepted=entry->match.roster(std::span(members).first(in.count),in.expected,now);
    entry->match.tick(now);out=entry->match.view();
    if(out.phase!=prior) {
        std::array<char,256> line{};
        const auto n=std::snprintf(line.data(),line.size(),
            "ev=pvp stage=match phase=%s session=0x%llX owner=0x%llX players=%u limit=%u duration_ms=%llu",
            out.phase==host::Phase::playing?"playing":"results",in.key.session,in.key.owner,
            unsigned(out.count),out.scoreLimit,out.durationMs);
        if(n>0) core::log::write(core::log::Channel::server,core::log::Level::info,
            {line.data(),(std::min)(static_cast<std::size_t>(n),line.size()-1)});
    }
    if(!rosterAccepted && out.phase==host::Phase::playing)
        core::log::write(core::log::Channel::server,core::log::Level::warn,
            "ev=pvp stage=roster result=refused reason=changed_match_roster");
    if(out.phase==host::Phase::results && !entry->archived) entry->archived=archive(out);
    return true;
}
}
bool publication(std::uint64_t session,bool localReady,host::View& out) noexcept {
    out={};Input in{};if(!gather(session,in)) return false;
    AcquireSRWLockExclusive(&lock);
    const bool ok=update(in,localReady,GetTickCount64(),out);
    ReleaseSRWLockExclusive(&lock);return ok;
}
void observe_incident(std::uint64_t session,std::span<const std::byte> payload) noexcept {
    namespace wire=middleware::bap::activity_message::incident;
    wire::Incident incident{};
    if(wire::validate(payload,incident)!=wire::Verdict::accepted
        || incident.primaryTarget!=death_receipt::kTarget || !incident.hasPayload) return;
    death_receipt::Receipt receipt{};
    if(!death_receipt::decode(std::span(incident.payload).first(incident.payloadLength),receipt)) return;
    Input in{};if(!gather(session,in)) return;
    // the dying player's own connection publishes this receipt; a killer or bystander must not
    // be able to assert a death on another account
    if(in.account!=receipt.victim.account) return;
    std::uint64_t digest=14695981039346656037ULL;
    for(auto b:payload) digest=(digest^std::to_integer<unsigned char>(b))*1099511628211ULL;
    const auto now=GetTickCount64();bool scored=false;host::View view{};
    AcquireSRWLockExclusive(&lock);
    for(auto& entry:matches) if(entry.match.view().key==in.key) {
        const bool killer=std::any_of(in.members.begin(),in.members.begin()+in.count,
            [&](const host::Member& m) { return m.account==receipt.killer.account
                && m.player==receipt.killer.player; });
        const bool victim=std::any_of(in.members.begin(),in.members.begin()+in.count,
            [&](const host::Member& m) { return m.account==receipt.victim.account
                && m.player==receipt.victim.player; });
        auto members=in.members;
        for(std::size_t i=0;i<in.count;++i) {
            bool ready=false;
            for(std::size_t j=0;j<12;++j) if(entry.readyAccounts[j]==members[i].account)
                ready=entry.readyTicks[j] && now>=entry.readyTicks[j] && now-entry.readyTicks[j]<20'500;
            members[i].ready&=ready;
        }
        const bool rosterValid=entry.match.view().phase==host::Phase::playing
            && entry.match.roster(std::span(members).first(in.count),in.expected,now);
        if(killer && victim && rosterValid) scored=entry.match.kill(receipt.killer.account,
            receipt.victim.player,receipt.sequence,digest,now);
        view=entry.match.view();break;
    }
    ReleaseSRWLockExclusive(&lock);
    std::array<char,384> line{};
    const int n=std::snprintf(line.data(),line.size(),
        "ev=pvp stage=kill_receipt result=%s session=0x%llX reporter=0x%llX killer=0x%llX victim=0x%llX sequence=%u revision=%u mapping=1412_victim_report",
        scored?"scored":"ignored",session,in.account,receipt.killer.player,
        receipt.victim.player,receipt.sequence,view.revision);
    if(n>0) core::log::write(core::log::Channel::server,core::log::Level::info,
        {line.data(),(std::min)(static_cast<std::size_t>(n),line.size()-1)});
}
bool for_account(std::uint64_t account,host::View& out) noexcept {
    out={};if(!account) return false;const auto now=GetTickCount64();std::uint64_t latest{};
    AcquireSRWLockExclusive(&lock);
    for(auto& entry:matches) {
        if(!entry.used || now<entry.used || now-entry.used>120'000) continue;
        entry.match.tick(now);const auto v=entry.match.view();
        for(std::size_t i=0;i<v.count;++i) if(v.rows[i].account==account && entry.created>=latest) {
            latest=entry.created;out=v;break;
        }
        if(v.phase==host::Phase::results && !entry.archived) entry.archived=archive(v);
    }
    ReleaseSRWLockExclusive(&lock);return out.phase!=host::Phase::absent;
}
}
