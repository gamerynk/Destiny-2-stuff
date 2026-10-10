#include "activity_retirement_push.h"
#include <Windows.h>
#include "../../../../../middleware/secure_channel/runtime.h"
#include "../../../../../core/logging/log.h"
#include <algorithm>
#include "../../../../../middleware/bap/activity_message/departure_query_wire.h"

namespace dawn::server::bap::encrypted::push::activity {
namespace retirement=state::activity::entity_retirement;
bool consume_departure_query(Session& session,Scratch& scratch,std::span<std::byte> response,
                              std::size_t& written,bool& touchesScratch) noexcept {
    retirement::Queries::Plan plan{};
    if (!session.authenticated || !retirement::next_query(session.activitySessionId,
        session.credentials.accountSoid,session.activityRetirementJoinRevision,GetTickCount64(),plan)) return false;
    touchesScratch=true;
    auto nonce=session.sendNonce;
    std::array<std::byte,4> body{};
    std::size_t bodySize{},size{};
    const bool staged=retirement::wire::encode_query(plan.correlation,body,bodySize)
        && append_notification_frame(scratch,session.activitySessionId,30,body,
            session.credentials.bap.sessionKey,nonce,scratch.framed,size);
    bool published=false;
    if (staged && size!=0 && size<=response.size()) {
        middleware::secure_channel::advance_nonce(nonce);
        struct Copy {
            Session& session; Scratch& scratch; std::span<std::byte> response;
            std::size_t size; std::size_t& written;
            const std::array<std::byte,state::kBapNonceSize>& nonce;
        } context{session,scratch,response,size,written,nonce};
        published=retirement::publish_query(session.activitySessionId,plan,GetTickCount64(),
            [](void* raw) noexcept {
                auto& c=*static_cast<Copy*>(raw);
                std::copy_n(c.scratch.framed.begin(),c.size,c.response.begin());
                c.written=c.size;c.session.sendNonce=c.nonce;
            },&context);
    }
    SecureZeroMemory(&nonce,sizeof nonce);
    SecureZeroMemory(scratch.framed.data(),size);
    if (published) core::log::write(core::log::Channel::server,core::log::Level::info,
        "ev=activity stage=departure_cleanup result=query_published type=30");
    return published;
}
bool append_retirement(Scratch& scratch,std::uint64_t activity,
                        const retirement::History::Operation& op,
                        std::span<const std::byte,state::kAesKeySize> key,
                        std::array<std::byte,state::kBapNonceSize>& nonce,
                        std::span<std::byte> output,std::size_t& written) noexcept {
    if (!op.serial || written>output.size()) return false;
    std::size_t bodySize{};
    const bool ok=retirement::wire::encode_purge(op.mask,op.reason,
        static_cast<std::uint8_t>(op.serial&0xFF),scratch.responseBody,bodySize)
        && append_notification_frame(scratch,activity,25,
            std::span(scratch.responseBody).first(bodySize),key,nonce,output,written);
    SecureZeroMemory(scratch.responseBody.data(),bodySize);
    if (ok) middleware::secure_channel::advance_nonce(nonce);
    return ok;
}
bool consume_retirement(Session& session,Scratch& scratch,std::span<std::byte> response,
                         std::size_t& written,bool& touchesScratch) noexcept {
    retirement::History::Operation op{};
    if (!session.authenticated || !retirement::next(session.activitySessionId,
        session.credentials.accountSoid,session.activityRetirementJoinRevision,op)) return false;
    touchesScratch=true;
    auto nonce=session.sendNonce;
    std::size_t size{};
    const bool staged=append_retirement(scratch,session.activitySessionId,op,
        session.credentials.bap.sessionKey,nonce,scratch.framed,size);
    bool published=false;
    if (staged && size!=0 && size<=response.size()) {
        struct Copy {
            Session& session; Scratch& scratch; std::span<std::byte> response;
            std::size_t size; std::size_t& written;
            const std::array<std::byte,state::kBapNonceSize>& nonce;
            std::uint64_t serial;
        } context{session,scratch,response,size,written,nonce,op.serial};
        published=retirement::publish(session.activitySessionId,session.credentials.accountSoid,
            session.activityRetirementJoinRevision,op.serial,[](void* raw) noexcept {
                auto& c=*static_cast<Copy*>(raw);
                std::copy_n(c.scratch.framed.begin(),c.size,c.response.begin());
                c.written=c.size;
                c.session.sendNonce=c.nonce;
                c.session.activityPublishedRetirementSerial=c.serial;
            },&context);
    }
    SecureZeroMemory(&nonce,sizeof nonce);
    SecureZeroMemory(scratch.framed.data(),size);
    if (published) core::log::write(core::log::Channel::server,core::log::Level::info,
        "ev=activity stage=retirement result=published type=25");
    return published;
}
}
