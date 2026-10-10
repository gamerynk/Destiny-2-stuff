#pragma once
#include <cstring>
#include <cstdio>
#include "fixtures/native_death_receipts.h"
#include "state/activity/crucible/native_death_receipt.h"
#include "middleware/bap/activity_message/incident.h"
#include "state/activity/crucible/match_runtime.h"
#include "state/activity/shared/runtime.h"
#include "state/runtime/runtime.h"
#include "state/runtime/storage/internal.h"
#include "middleware/encoding/bit_writer.h"
#include "server/bap/encrypted/push/activity/activity_roster_push.h"
#include "server/http/server_http.h"
#include "../restorationserver/src/account_http.h"

namespace restoration::tests {
inline bool crucible_runtime_self_test() noexcept {
    using namespace dawn;
    namespace activity=state::activity;
    namespace host=activity::crucible::host;
    namespace runtime=activity::crucible::runtime;
    static std::array<state::AccountState,5> accounts{};
    static std::array<activity::ActivityState,5> worlds{};
    std::array<state::AccountRuntimeContext,5> contexts{};
    const auto check=[](bool ok,const char* label) {
        if(!ok) std::printf("FAIL: Crucible runtime: %s\n",label);return ok;
    };
    if(!check(server::http::register_extension_consumer(&account_http::consume),
        "register HTTP extension")) return false;
    bool publicRouteOk{};
    {
        struct HttpCleanup { ~HttpCleanup() {
            server::http::unregister_extension_consumer(&account_http::consume);
        }} httpCleanup;
        std::array<std::byte,256> responseBytes{};
        client::network::HttpRequest request{client::network::HttpMethod::post,
            "/v1/crucible/match","application/octet-stream",{},{},responseBytes};
        client::network::HttpResponse response{};
        publicRouteOk = server::http::consume_public(request,response)
            && response.statusCode == 401;
        request.method=client::network::HttpMethod::get;
        publicRouteOk = publicRouteOk && !server::http::consume_public(request,response);
        request.method=client::network::HttpMethod::post;
        request.url="/v1/crucible/match/private";
        publicRouteOk = publicRouteOk && !server::http::consume_public(request,response);
    }
    if(!check(publicRouteOk,"public match endpoint authenticates; other paths denied")) return false;
    static server::bap::Session link{};
    link = {};
    link.activityCruciblePhase = static_cast<std::uint8_t>(host::Phase::playing);
    const auto stageResult = [&] {
        link.activityRosterStaged.staged = true;
        link.activityRosterStaged.crucible = true;
        link.activityRosterStaged.cruciblePhase = static_cast<std::uint8_t>(host::Phase::results);
        link.activityRosterStaged.crucibleCompletionPhase = 6;
        link.activityRosterStaged.crucibleEndTick = 100;
        link.activityRosterStaged.crucibleRevision = 4;
    };
    stageResult();
    server::bap::encrypted::push::activity::discard_staged_roster(link);
    if(!check(link.activityCruciblePhase == static_cast<std::uint8_t>(host::Phase::playing)
        && link.activityCrucibleEndTick == 0 && link.activityCrucibleRevision == 0
        && link.activityCrucibleCompletionPhase == 0, "discard leaves result retry armed")) return false;
    stageResult();
    server::bap::encrypted::push::activity::commit_staged_roster(link);
    if(!check(link.activityCruciblePhase == static_cast<std::uint8_t>(host::Phase::results)
        && link.activityCrucibleEndTick == 100 && link.activityCrucibleRevision == 4
        && link.activityCrucibleCompletionPhase == 6, "delivery begins completion interval")) return false;
    stageResult();link.activityRosterStaged.crucibleCompletionPhase = 7;
    server::bap::encrypted::push::activity::discard_staged_roster(link);
    if(!check(link.activityCrucibleCompletionPhase == 6, "lost results transition remains owed")) return false;
    stageResult();link.activityRosterStaged.crucibleCompletionPhase = 7;
    server::bap::encrypted::push::activity::commit_staged_roster(link);
    if(!check(link.activityCrucibleCompletionPhase == 7 && link.activityCrucibleEndTick == 100,
        "results delivery advances phase without restarting timer")) return false;
    constexpr std::array<std::uint64_t,4> accountIds{0x9EAA000100200100ULL,0x9EAA000100E00100ULL,0x9EAA000101000100ULL,0x9EAA000101100100ULL};
    constexpr std::array<std::uint64_t,4> playerIds{0x9EAA000100200102ULL,0x9EAA000100E00101ULL,0x9EAA000101000101ULL,0x9EAA000101100101ULL};
    AcquireSRWLockExclusive(&state::runtime::storage::g_stateLock);
    activity::shared::reset_locked();
    for(std::size_t i=0;i<4;++i) {
        accounts[i]={};worlds[i]={};accounts[i].primarySoid=accountIds[i];
        contexts[i]={&accounts[i],&worlds[i]};
        auto& r=worlds[i].sessions[0];
        r.sessionId=i<2?0x9911:0x9922;r.ownerAccountSoid=accounts[i].primarySoid;
        r.sharedOwnerAccountSoid=accountIds[i/2*2];
        r.createdRevision=r.sharedOwnerCreatedRevision=1;r.recordRevision=2;r.joinedRevision=2;
        r.sharedMemberSlot=static_cast<std::uint8_t>(i%2);r.sharedLeaseParts=12;
        r.occupied=r.joined=r.membership.hasIdentity=true;r.memberKey=1000+i;
        r.membership.identity={r.memberKey,0,0,0xB918CC084C648DAFULL+i,
            r.ownerAccountSoid,playerIds[i],0};
        r.membership.region.index=0;
        const auto team=static_cast<std::int8_t>(i<2?i%2:0);
        r.membership.peerConnection.playerTeam={team,team,true,true};
        r.destination.previousActivityIndex=i<2?497:502;r.destination.activityIndex=475;
        r.destination.menuOptionCount=2;
        r.destination.menuOptions[0]={64,static_cast<std::int16_t>(i%2?2:1)};
        r.destination.menuOptions[1]={65,static_cast<std::int16_t>(i%2?0:3)};
        constexpr char name[]="pvp_shaft";
        std::memcpy(r.destination.packageName.data(),name,sizeof(name)-1);
        r.destination.packageNameLength=sizeof(name)-1;
        activity::shared::register_context_locked(contexts[i]);
    }
    accounts[4]={};worlds[4]={};accounts[4].primarySoid=0x9EAA000101300100ULL;
    contexts[4]={&accounts[4],&worlds[4]};
    auto& solo=worlds[4].sessions[0];solo=worlds[0].sessions[0];
    solo.sessionId=0x9933;solo.ownerAccountSoid=solo.sharedOwnerAccountSoid=accounts[4].primarySoid;
    solo.destination.previousActivityIndex=502;solo.destination.menuOptions[1]={65,1};
    solo.memberKey=2000;
    solo.membership.identity={solo.memberKey,0,0,0xB918CC084C648DBFULL,
        solo.ownerAccountSoid,0x9EAA000101300102ULL,0};
    activity::shared::register_context_locked(contexts[4]);
    ReleaseSRWLockExclusive(&state::runtime::storage::g_stateLock);
    struct Cleanup { ~Cleanup() {
        AcquireSRWLockExclusive(&state::runtime::storage::g_stateLock);
        activity::shared::reset_locked();ReleaseSRWLockExclusive(&state::runtime::storage::g_stateLock);
    }} cleanup;
    host::View v{};
    {
        const state::ScopedAccountRuntime scope(contexts[4]);
        if(!check(runtime::publication(0x9933,false,v) && v.phase==host::Phase::waiting,
            "solo private activity waits for world readiness")) return false;
        if(!check(runtime::publication(0x9933,true,v) && v.phase==host::Phase::playing
            && v.count==1 && v.scoreLimit==25 && v.durationMs==480'000 && v.remainingMs==480'000,
            "solo private activity starts the selected eight-minute clock")) return false;
    }
    for(std::size_t i=0;i<4;++i) {
        const state::ScopedAccountRuntime scope(contexts[i]);
        if(!check(runtime::publication(i<2?0x9911:0x9922,true,v),"publish authenticated private activity")) return false;
        if(!check(v.scoreLimit==25 && v.durationMs==900'000,"host menu choices retained across peer publication")) return false;
        if(!check(v.phase==(i%2==0?host::Phase::waiting:host::Phase::playing),"Clash and native-team-zero Rumble start after both clients ready")) return false;
    }
    namespace wire=middleware::bap::activity_message::incident;
    for (auto raw : {std::span<const std::byte>(fixtures::kDeathNoAssist),
                    std::span<const std::byte>(fixtures::kDeathWithAssist)}) {
        wire::Incident decoded{};activity::crucible::death_receipt::Receipt death{};
        if(!check(wire::validate(raw,decoded)==wire::Verdict::accepted
            && decoded.primaryTarget==1412 && decoded.hasCompressedSelector && decoded.hasPayload
            && activity::crucible::death_receipt::decode(std::span(decoded.payload).first(decoded.payloadLength),death)
            && death.killer.account==accountIds[0] && death.victim.account==accountIds[1]
            && death.killer.player==playerIds[0] && death.victim.player==playerIds[1],
            "captured native receipt with compressed selector and optional assists")) return false;
        for(std::size_t length=0;length<decoded.payloadLength;++length)
            if(!check(!activity::crucible::death_receipt::decode(std::span(decoded.payload).first(length),death),
                "every truncated native death fails closed")) return false;
    }
    {
        wire::Incident decoded{};
        const auto raw=std::span<const std::byte>(fixtures::kDeathNoAssist);
        for(std::size_t length=0;length<raw.size();++length)
            if(!check(wire::validate(raw.first(length),decoded)!=wire::Verdict::accepted,
                "truncated outer incident is rejected")) return false;
        std::array<std::byte,8> malformed{};std::size_t size{};
        middleware::encoding::bits::Writer writer(malformed);
        if(!check(writer.write(1412,13)&&writer.write(0,5)&&writer.write(1,1)
            &&writer.write(261,9)&&writer.finish(size)
            &&wire::validate(std::span(malformed).first(size),decoded)==wire::Verdict::selectorTooLong,
            "compressed selector maximum is enforced")) return false;
    }
    const auto& bytes=fixtures::kDeathNoAssist;const auto n=bytes.size();
    {
        const state::ScopedAccountRuntime scope(contexts[0]);
        runtime::observe_incident(0x9911,std::span(bytes).first(n));
    }
    if(!check(runtime::for_account(accounts[0].primarySoid,v) && v.rows[0].kills==0,
        "killer cannot report victim's death")) return false;
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        runtime::observe_incident(0x9911,std::span(bytes).first(n));
        runtime::observe_incident(0x9911,std::span(bytes).first(n));
    }
    if(!check(runtime::for_account(accounts[0].primarySoid,v) && v.rows[0].kills==1 && v.rows[1].deaths==1,
        "one attributed kill despite duplicate")) return false;
    if(!check(runtime::for_account(accounts[2].primarySoid,v) && v.scores[0]==0 && v.scores[1]==0,
        "concurrent lobby unchanged")) return false;
    {
        const state::ScopedAccountRuntime scope(contexts[2]);
        runtime::observe_incident(0x9911,std::span(bytes).first(n));
        runtime::observe_incident(0x9922,std::span(bytes).first(n));
    }
    if(!check(runtime::for_account(accounts[0].primarySoid,v) && v.rows[0].kills==1
        && !runtime::for_account(123,v),"foreign session and result-reader denied")) return false;
    AcquireSRWLockExclusive(&state::runtime::storage::g_stateLock);
    worlds[1].sessions[0].joined=false;
    ReleaseSRWLockExclusive(&state::runtime::storage::g_stateLock);
    {
        const state::ScopedAccountRuntime scope(contexts[1]);
        runtime::observe_incident(0x9911,std::span(bytes).first(n));
        if(!check(!runtime::publication(0x9911,true,v),"departed member cannot publish")) return false;
    }
    std::puts("PASS: real shared-activity admission, two concurrent match registries, captured native death receipts and result access");
    return true;
}
}
