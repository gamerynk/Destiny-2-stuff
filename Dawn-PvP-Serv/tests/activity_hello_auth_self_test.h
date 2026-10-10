#pragma once
#include <algorithm>
#include <array>
#include <cstdio>
#include "server/bap/internal.h"
#include "state/matchmaking/matchmaking_state.h"
#include "middleware/protobuf/codec.h"

namespace restoration::tests {
inline bool activity_hello_auth_self_test(const dawn::state::session::Credentials& issued) noexcept {
    using namespace dawn;
    static server::bap::Scratch scratch{};
    static server::bap::Session session{};
    std::array<std::byte,512> payload{}, response{}, baseline{};
    std::size_t baselineSize{};
    const auto run=[&](unsigned version,bool extended,bool badToken,bool badExtension) noexcept {
        session={}; session.id=1;
        payload.fill(std::byte{}); response.fill(std::byte{});
        payload[1]=std::byte{25}; payload[5]=std::byte{1};
        middleware::protobuf::Writer w(std::span(payload).subspan(6));
        if (!w.write_length_delimited(1,issued.signOn.sessionToken) || !w.write_varint(2,version)
            || (extended && !w.write_varint(3,0x9EAA00010020000BULL))) return false;
        if (badToken) payload[8]^=std::byte{1};
        if (badExtension) payload[6+36]=std::byte{0x10};
        const middleware::bap::OuterFrame frame{middleware::bap::FrameType::plaintext0,
            std::span(payload).first(6+w.size())};
        std::size_t written{};
        const bool accepted=server::bap::plaintext::consume(session,scratch,frame,response,written);
        const bool expected=!badToken && !badExtension;
        bool ok=accepted==expected && session.authenticated==expected;
        if (expected) {
            ok=ok && written>0 && session.credentials.accountSoid==issued.accountSoid
                && session.credentials.platformId==issued.platformId
                && session.credentials.generation==issued.generation
                && session.accountRuntime.account && session.accountRuntime.activity
                && session.accountRuntime.account->primarySoid==issued.accountSoid;
            if (baselineSize==0) {baseline=response; baselineSize=written;}
            else ok=ok && written==baselineSize
                && std::equal(response.begin(),response.begin()+written,baseline.begin());
            ok=ok && !server::bap::plaintext::consume(session,scratch,frame,response,written)
                && written==0;
        } else ok=ok && written==0
            && session.matchmakingContext.generation==state::matchmaking::kInvalidGeneration
            && !session.accountRuntime.account && !session.accountRuntime.activity;
        if (session.matchmakingContext.generation!=state::matchmaking::kInvalidGeneration)
            ok=state::matchmaking::release_context(session.matchmakingContext) && ok;
        SecureZeroMemory(&session,sizeof(session));
        SecureZeroMemory(payload.data(),payload.size());
        SecureZeroMemory(response.data(),response.size());
        return ok;
    };
    const bool ok=run(1,false,false,false) && run(2,false,false,false)
        && run(3,true,false,false) && run(2,true,false,false)
        && run(2,true,true,false) && run(2,true,false,true);
    SecureZeroMemory(baseline.data(),baseline.size());
    std::puts(ok?"PASS: real hello route baselines/follower, identical reply, token/account binding and rejection"
                :"FAIL: real activity hello authentication route");
    return ok;
}
}
