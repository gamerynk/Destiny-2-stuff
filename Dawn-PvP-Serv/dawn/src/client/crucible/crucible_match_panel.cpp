#include "match_panel.h"
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <imgui.h>
#include "../hooks/external_server/transport.h"
#include "../../core/identity/device_identity.h"
#include "../../core/identity/local_ticket.h"
#include "../../middleware/crucible/match_view_wire.h"
#include "../../steam/runtime/runtime.h"

namespace dawn::client::crucible {
namespace {
namespace wire=middleware::crucible::match_view_wire;
namespace host=state::activity::crucible::host;
SRWLOCK lock=SRWLOCK_INIT;
host::View current{};HANDLE worker{};std::uint64_t nextPoll{},received{};
bool stopped{};
DWORD WINAPI poll(void*) noexcept {
    namespace device=core::identity::device_identity;
    std::array<std::byte,device::kProofSize> proof{};
    std::size_t proofSize{};
    const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    bool signedRequest=false;
    if(seconds>0) {
        if constexpr(steam::kUniversalClientBuild) {
            proofSize=proof.size();signedRequest=device::issue(device::Scope::crucibleMatch,seconds,proof);
        } else {
            proofSize=core::identity::local_ticket::kTicketSize;
            signedRequest=core::identity::local_ticket::issue(steam::local_steam_id(),seconds,
                std::span<std::byte,core::identity::local_ticket::kTicketSize>(proof.data(),proofSize));
        }
    }
    std::array<std::byte,wire::kMaximum> bytes{};network::HttpResponse response{};host::View view{};
    const network::HttpRequest request{network::HttpMethod::post,"/v1/crucible/match","application/octet-stream",{},
        std::span(proof).first(proofSize),bytes};
    const bool ok=signedRequest && hooks::external_server::consume(request,response)
        && response.statusCode==200 && response.size<=bytes.size()
        && wire::decode(std::span(bytes).first(response.size),view);
    SecureZeroMemory(proof.data(),proof.size());
    AcquireSRWLockExclusive(&lock);
    if(ok && !stopped) { current=view;received=GetTickCount64(); }
    ReleaseSRWLockExclusive(&lock);return 0;
}
}
void activate() noexcept {
    AcquireSRWLockExclusive(&lock);stopped=false;nextPoll=0;ReleaseSRWLockExclusive(&lock);
}
void service([[maybe_unused]] std::uint64_t now) noexcept {
#if !defined(RESTORATION_DEDICATED_SERVER)
    AcquireSRWLockExclusive(&lock);
    if(worker && WaitForSingleObject(worker,0)==WAIT_OBJECT_0) { CloseHandle(worker);worker=nullptr; }
    if(!stopped && !worker && now>=nextPoll) {
        worker=CreateThread(nullptr,0,&poll,nullptr,0,nullptr);
        nextPoll=now+(current.phase==host::Phase::playing?500:2'000);
    }
    ReleaseSRWLockExclusive(&lock);
#endif
}
void shutdown() noexcept {
    AcquireSRWLockExclusive(&lock);stopped=true;const auto pending=worker;ReleaseSRWLockExclusive(&lock);
    if(pending) WaitForSingleObject(pending,INFINITE);
    AcquireSRWLockExclusive(&lock);if(worker) CloseHandle(worker);worker=nullptr;current={};received=0;
    ReleaseSRWLockExclusive(&lock);
}
bool draw() noexcept {
    if ((GetAsyncKeyState(VK_F10)&0x8000)==0) return false;
    host::View v{};std::uint64_t seen{};
    AcquireSRWLockShared(&lock);v=current;seen=received;ReleaseSRWLockShared(&lock);
    const auto now=GetTickCount64();
    if(v.phase==host::Phase::absent || !seen || now<seen || now-seen>10'000) return false;
    const bool ended=v.phase==host::Phase::results;
    const auto delta=v.phase==host::Phase::playing?now-seen:0;
    const auto remaining=v.remainingMs>(delta)?v.remainingMs-delta:0;
    const auto seconds=(remaining+999)/1000;
    const auto* viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x+viewport->WorkSize.x*0.5F,viewport->WorkPos.y+58),
        ImGuiCond_Always,ImVec2(0.5F,0));
    ImGui::SetNextWindowBgAlpha(0.80F);
    constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoSavedSettings
        |ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoNav;
    if(ImGui::Begin("##dawn_match",nullptr,flags)) {
        const bool clash=v.mode==state::activity::crucible::match_rules::Mode::clash;
        ImGui::Text("%s  |  %s",clash?"CLASH":"RUMBLE",ended?"MATCH COMPLETE":v.phase==host::Phase::waiting?"WAITING FOR PLAYERS":"PRIVATE MATCH");
        if(v.phase==host::Phase::playing) ImGui::Text("%llu:%02llu  |  First to %u",seconds/60,seconds%60,v.scoreLimit);
        if(clash) ImGui::Text("Alpha  %u     -     %u  Bravo",v.scores[0],v.scores[1]);
        if(ended) {
            if(v.winner==255) ImGui::TextUnformatted("DRAW");
            else if(clash) ImGui::Text("%s WINS",v.winner==0?"ALPHA":"BRAVO");
            else for(std::size_t i=0;i<v.count;++i) if(v.rows[i].side==v.winner)
                ImGui::Text("Winner: %s",v.rows[i].name.data());
        }
        const bool details=ended || (GetAsyncKeyState(VK_F10)&0x8000)!=0;
        if(details && ImGui::BeginTable("##match_scores",4,ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Player");ImGui::TableSetupColumn("Score");ImGui::TableSetupColumn("Kills");ImGui::TableSetupColumn("Deaths");ImGui::TableHeadersRow();
            auto rows=v.rows;
            std::stable_sort(rows.begin(),rows.begin()+v.count,[](const auto& a,const auto& b) {return a.score>b.score;});
            for(std::size_t i=0;i<v.count;++i) {
                const auto& r=rows[i];ImGui::TableNextRow();ImGui::TableNextColumn();
                if(r.name[0]) ImGui::TextUnformatted(r.name.data());else ImGui::Text("Player %zu",i+1);
                ImGui::TableNextColumn();ImGui::Text("%u",r.score);ImGui::TableNextColumn();ImGui::Text("%u",r.kills);
                ImGui::TableNextColumn();ImGui::Text("%u",r.deaths);
            }
            ImGui::EndTable();
        } else if(!ended) {
            if(!clash) {
                std::uint32_t leader{};for(auto score:v.scores) leader=(std::max)(leader,score);
                ImGui::Text("Leader: %u",leader);
            }
            ImGui::TextDisabled("Hold F10 for scores");
        }
    }
    ImGui::End();return true;
}
bool try_snapshot(host::View& view, std::uint64_t& receivedAt) noexcept {
    if (!TryAcquireSRWLockShared(&lock)) return false;
    view=current;receivedAt=received;
    const bool available=!stopped;
    ReleaseSRWLockShared(&lock);
    return available;
}
}
