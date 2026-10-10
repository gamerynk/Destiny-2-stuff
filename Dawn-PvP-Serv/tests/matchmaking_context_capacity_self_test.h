#pragma once
#include <array>
#include <cstdio>
#include "server/bap/internal.h"
#include "state/matchmaking/matchmaking_state.h"
#include "state/runtime/storage/internal.h"
#include "middleware/protobuf/codec.h"

namespace restoration::tests {
inline bool matchmaking_context_capacity_self_test(
    const dawn::state::session::Credentials& issued) noexcept {
    using namespace dawn;
    namespace mm=state::matchmaking;
    static mm::MatchmakingState saved{};
    saved=state::runtime::storage::g_state.matchmaking;
    struct Restore {
        ~Restore() {
            SecureZeroMemory(&state::runtime::storage::g_state.matchmaking,sizeof(saved));
            state::runtime::storage::g_state.matchmaking=saved;
            SecureZeroMemory(&saved,sizeof(saved));
        }
    } restore;
    state::runtime::storage::g_state.matchmaking={};
    static server::bap::Scratch scratch{};
    static server::bap::Session session{};
    std::array<mm::ContextHandle,mm::kContextCapacity> handles{};
    std::array<std::uint64_t,mm::kContextCapacity> advertisementIds{};
    std::array<std::byte,512> payload{},response{};
    struct Erase {
        ~Erase() { SecureZeroMemory(&session,sizeof(session)); }
    } erase;
    payload[1]=std::byte{25};payload[5]=std::byte{1};
    middleware::protobuf::Writer writer(std::span(payload).subspan(6));
    if (!writer.write_length_delimited(1,issued.signOn.sessionToken)
        || !writer.write_varint(2,2) || !writer.write_varint(3,0x9EAA000100200010ULL)) return false;
    const middleware::bap::OuterFrame frame{middleware::bap::FrameType::plaintext0,
        std::span(payload).first(6+writer.size())};
    const auto check=[](bool value,const char* reason) noexcept {
        if (!value) std::printf("FAIL: matchmaking context capacity: %s\n",reason);
        return value;
    };
    const auto hello=[&](std::span<std::byte> output) noexcept {
        SecureZeroMemory(&session,sizeof(session));session.id=1;
        std::size_t written=999;
        const bool result=server::bap::plaintext::consume(session,scratch,frame,output,written);
        return result && session.authenticated && written>0;
    };
    bool ok=check(mm::kContextCapacity>=server::bap::kSessionCount,"transport parity");
    for (std::size_t i=0;ok && i<handles.size();++i) {
        ok=check(hello(response),"all concurrent authenticated channels admitted");
        if (!ok) break;
        handles[i]=session.matchmakingContext;
        for (std::size_t j=0;j<i;++j)
            ok=check(handles[i].slot!=handles[j].slot,"distinct live contexts") && ok;
        mm::PendingMutation plan{};
        ok=check(mm::prepare_initial_latest(handles[i],advertisementIds[i],plan)
                 && mm::commit(plan),"independent advertisement") && ok;
    }
    if (ok) ok=check(!hello(response) && !session.authenticated
        && session.matchmakingContext.generation==0,"exhaustion rejects without authentication");
    for (std::size_t i=0;ok && i<handles.size();++i) {
        mm::LatestSnapshot latest{};
        ok=check(mm::latest_snapshot(handles[i],latest)
            && latest.advertisementId==advertisementIds[i],"exhaustion preserves live state");
        mm::erase_snapshot(latest);
    }
    if (ok) {
        const auto stale=handles[4];
        ok=check(mm::release_context(stale),"release fifth/public channel");
        std::array<std::byte,1> tooSmall{};
        ok=check(!hello(tooSmall) && !session.authenticated,"encoding failure remains unauthenticated") && ok;
        ok=check(hello(response),"encoding failure releases provisional context") && ok;
        const auto recycled=session.matchmakingContext;
        ok=check(recycled.slot==stale.slot && recycled.generation>stale.generation
            && !mm::release_context(stale),"recycled slot rejects stale handle") && ok;
        handles[4]=recycled;
    }
    for (const auto handle:handles)
        if (handle.generation!=0) ok=check(mm::release_context(handle),"release channels") && ok;
    SecureZeroMemory(payload.data(),payload.size());
    SecureZeroMemory(response.data(),response.size());
    std::puts(ok?"PASS: 32 real BAP hellos, context isolation/exhaustion/recycle/encode rollback"
                :"FAIL: BAP matchmaking context capacity");
    return ok;
}
}
