#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../dawn/src/server/bap/encrypted/encrypted_runtime.cpp"
#include "../dawn/src/server/bap/encrypted/queuez/staging/queuez_subscription_progress.h"
#include "../dawn/src/server/bap/encrypted/queuez/staging/queuez_family_staging.h"
#include "../dawn/src/middleware/encoding/bit_reader.h"
#include "../dawn/src/middleware/encoding/bit_writer.h"
#include "../dawn/src/middleware/encoding/byte_order.h"
#include "../dawn/src/middleware/web_service/messages/opcode206.h"
#include "../dawn/src/server/web_service/opcode_routes.h"

namespace e = dawn::server::bap::encrypted;
namespace b = dawn::server::bap;
namespace m = dawn::middleware;
namespace ws = m::web_service;
namespace q = e::queuez;
namespace fixture {
bool active = true, bodyOkay = true, stageOkay = true, commitOkay = true, suppress = false, replacementFailure = false;
int commits{}, discards{}, publishes{}, bodyCalls{}, rosterCommits{};
bool sawInline{};
 m::bap::activity_message::RequestProvenance provenance{};
std::array<std::byte,25> prepared{};
ws::StatusResponse status{7, 0x1234567, true};
int checks{};
void check(bool value, const char* label) {
    ++checks;
    if (!value) { std::printf("FAIL %d: %s\n",checks,label); std::exit(1); }
}
void reset() {
    active=bodyOkay=stageOkay=commitOkay=true; suppress=replacementFailure=false;
    commits=discards=publishes=bodyCalls=rosterCommits=0; sawInline=false;
}
}
namespace dawn::core::log { void write(Channel,Level,std::string_view) noexcept {} }
namespace dawn::state::session { bool active(const Credentials&,std::uint64_t) noexcept { return fixture::active; } }
namespace dawn::server::bap::encrypted {
namespace diagnostics { void report_failure(std::uint16_t,std::string_view) noexcept {} }
ConnectionFields connection_fields(const ServiceOutcome&) noexcept { return {}; }
void publish_connection_fields(Session&,const transactions::Publication&,const ConnectionFields&) noexcept { ++fixture::publishes; }
void arm_repushes(Session&,const queuez::StagedPublication&) noexcept {}
namespace transactions {
bool commit(ServiceOutcome&,Publication&) noexcept { ++fixture::commits; return fixture::commitOkay; }
}
namespace push::activity {
void commit_staged_roster(Session& s) noexcept { if(s.activityRosterStaged.staged) ++fixture::rosterCommits; s.activityRosterStaged.staged=false; }
void discard_staged_roster(Session& s) noexcept { ++fixture::discards; s.activityRosterStaged.staged=false; }
}
namespace activity_transaction {
bool stage_notifications(Session&,Scratch&,const activity_message::ActivityPlan&,
    std::span<const std::byte,state::kAesKeySize>,std::array<std::byte,state::kBapNonceSize>&,
    std::span<std::byte>,std::size_t&) noexcept { return false; }
}
namespace body {
bool process(const ServiceRoute&,const queuez::SessionState&,std::uint64_t,
    state::matchmaking::ContextHandle,std::uint64_t,std::uint64_t,
    std::span<const std::byte> input,std::span<std::byte> output,std::size_t& written,
    ServiceOutcome& outcome,std::uint64_t,
    const m::bap::activity_message::patch_epoch::PatchEpoch*,
    const m::bap::activity_message::RequestProvenance& provenance) noexcept {
    fixture::provenance=provenance;
    ++fixture::bodyCalls;
    if (!fixture::bodyOkay) return false;
    ws::Message message{};
    if (!ws::parse_request(input,message)) return false;
    outcome.hasSubscription = m::web_service::messages::opcode206::parse_request(message,outcome.subscription);
    ws::ResponseShape shape{};
    dawn::server::web_service::resolve_response_shape(message.opcode,shape);
    return ws::encode_response(message, shape,
        message.opcode==206 ? ws::StatusResponse{} : fixture::status,output,written);
}
}
namespace queuez {
bool stage_service_outcome(Scratch& scratch,const SessionState& before,const ServiceOutcome& outcome,
    std::span<const std::byte,state::kAesKeySize> key,std::array<std::byte,state::kBapNonceSize>& nonce,
    std::span<std::byte> output,std::size_t& written,StagedPublication& publication,
    InlineSubscriptionUpdate* update) noexcept {
    fixture::sawInline = update != nullptr;
    if (!fixture::stageOkay) return false;
    if (!outcome.hasSubscription || fixture::suppress) return true;
    std::size_t count{};
    if (!prepare_family3_subscription_progress(before,outcome.subscription,fixture::prepared,count,publication.after)) return false;
    publication.hasState=true;
    if (fixture::replacementFailure && update) {
        std::fill(update->bytes.begin(),update->bytes.end(),std::byte{0});
        update->size=update->bytes.size(); return true;
    }
    if (update) {
        if (update->bytes.size()<count) return false;
        std::copy_n(fixture::prepared.begin(),count,update->bytes.begin()); update->size=count;
    } else {
        std::size_t plain{}, sealed{}, framed{};
        if (!m::bap::encode_notification_payload(m::bap::NotificationService::queuezUpdate,0,
                fixture::prepared,scratch.responsePayload,plain)
            || !m::secure_channel::seal_frame(key,nonce,std::span(scratch.responsePayload).first(plain),scratch.sealed,sealed)
            || !m::bap::encode_frame(m::bap::FrameType::encrypted,std::span(scratch.sealed).first(sealed),output.subspan(written),framed)) return false;
        written+=framed; m::secure_channel::advance_nonce(nonce);
    }
    const std::array companion{std::byte{0xCA},std::byte{0xFE}};
    ServiceRoute route{ResponseMode::reply,m::bap::ResponseService::echo,BodyCodec::empty,{}};
    if (!reply::encode(scratch,route,0x87654321,key,nonce,companion,output,written)) return false;
    m::secure_channel::advance_nonce(nonce);
    return true;
}
}
}
namespace {
static b::Scratch scratch{};
static std::array<std::byte,4096> output{}, opened{};
static std::array<std::byte,256> requestBytes{}, sealedBytes{};
constexpr std::uint32_t task=0x10203040, transaction=0x12345678;
b::Session session() {
    b::Session result{}; result.authenticated=true; result.connectionIncarnation=9000;
    result.sendNonce[0]=std::byte{0x10}; result.receiveNonce[0]=std::byte{0x50};
    result.credentials.bap.sessionKey.fill(std::byte{0xAA});
    auto& state=result.queuez;
    state.family3Active=true; state.family3RootSoid=0x40; state.family3Version=7;
    state.family4Active=true; state.family4RootSoid=0x40; state.family4Version=3;
    state.family4ResidentCount=1;state.family4Residents[0]={0x40,1};
    state.family3Phase=q::Family3Phase::responseOnly;
    return result;
}
m::bap::OuterFrame request(const b::Session& s,std::uint16_t service,std::uint16_t opcode=206,bool shortSubscription=false) {
    auto bytes=std::span(requestBytes);
    m::encoding::write_u16_be(bytes.subspan<0,2>(),service);
    m::encoding::write_u32_be(bytes.subspan<2,4>(),task);
    m::encoding::write_u16_be(bytes.subspan<6,2>(),opcode);
    m::encoding::write_u32_be(bytes.subspan<8,4>(),transaction);
    m::encoding::bits::Writer writer(bytes.subspan(12));
    fixture::check(writer.write(4,4) && writer.write(0x40,64),"real subscription request fields");
    const std::size_t length=opcode==206 ? (shortSubscription ? 12U : 21U) : 12U;
    std::size_t sealed{};
    fixture::check(m::secure_channel::seal_frame(s.credentials.bap.sessionKey,s.receiveNonce,bytes.first(length),sealedBytes,sealed),"request AES-GCM");
    return {m::bap::FrameType::encrypted,std::span(sealedBytes).first(sealed)};
}
std::span<const std::byte> open(const b::Session& before,std::size_t offset,
    std::array<std::byte,dawn::state::kBapNonceSize> nonce,std::size_t& next) {
    m::bap::OuterFrame frame{};std::size_t count{};
    fixture::check(m::bap::parse_frame(std::span(output).subspan(offset),frame),"real outer frame");
    fixture::check(m::secure_channel::open_frame(before.credentials.bap.sessionKey,nonce,frame.payload,opened,count),"real response AES-GCM/nonce");
    next=offset+6+frame.payload.size(); return std::span(opened).first(count);
}
void successful(std::uint16_t service,bool enabled) {
    fixture::reset();auto s=session();const auto before=s;auto incoming=request(s,service);
    std::size_t written{};
    fixture::check(e::consume(s,scratch,incoming,output,written),"actual consume success");
    fixture::check(fixture::sawInline==enabled,"wire11 and112 eligibility" );
    fixture::check(fixture::provenance.connectionIncarnation==9000 && fixture::provenance.authenticatedOrdinal==1 && fixture::provenance.taskId==task,"authenticated opaque task provenance reaches body" );
    fixture::check(fixture::commits==1 && fixture::publishes==1 && fixture::discards==0,"one publication");
    auto expected=before.queuez;expected.family3Version++;
    fixture::check(q::staging::same_state(expected,s.queuez),"only genuine owned version advances");
    auto expectedReceive=before.receiveNonce;m::secure_channel::advance_nonce(expectedReceive);
    fixture::check(s.receiveNonce==expectedReceive,"authenticated receive nonce exactly once");
    auto nonce=before.sendNonce;std::size_t next{};
    auto response=open(before,0,nonce,next);
    fixture::check(m::encoding::read_u16_be(response.first<2>())==(service==110 ? 112 : 11)
        && m::encoding::read_u32_be(response.subspan<2,4>())==task
        && m::encoding::read_u16_be(response.subspan<6,2>())==200,"wire response/task/status exact");
    ws::Message parsed{};fixture::check(ws::parse_request(response.subspan(8),parsed)
        && parsed.opcode==206 && parsed.transactionId==transaction,"opcode/transaction exact");
    m::encoding::bits::Reader reader(parsed.payload);std::uint64_t field{};
    fixture::check(reader.read(5,field) && field==1,"status0 retains original5 bits");
    fixture::check(reader.read(1,field) && field==static_cast<unsigned>(enabled),"real first optional presence");
    if(enabled) {
        fixture::check(reader.read(16,field) && field==fixture::prepared.size(),"real prepared byte length");
        for(auto byte:fixture::prepared) fixture::check(reader.read(8,field) && field==std::to_integer<unsigned>(byte),"exact staged payload identity");
    }
    fixture::check(reader.read(1,field) && field==0,"second optional absent");
    m::secure_channel::advance_nonce(nonce);
    if(!enabled) {
        response=open(before,next,nonce,next);
        fixture::check(m::encoding::read_u16_be(response.first<2>())==123
            && response.size()==6+fixture::prepared.size()
            && std::equal(fixture::prepared.begin(),fixture::prepared.end(),response.begin()+6),"legacy notification unchanged");
        m::secure_channel::advance_nonce(nonce);
    }
    response=open(before,next,nonce,next);
    fixture::check(response.size()==10 && m::encoding::read_u16_be(response.first<2>())==251
        && m::encoding::read_u32_be(response.subspan<2,4>())==0x87654321
        && response[8]==std::byte{0xCA} && response[9]==std::byte{0xFE}
        && next==written,"trailing companion exact/order");
    m::secure_channel::advance_nonce(nonce);
    fixture::check(s.sendNonce==nonce,"one nonce per actual emitted frame");
    for(std::size_t capacity=0;capacity<written;++capacity) {
        fixture::reset();s=session();incoming=request(s,service);output.fill(std::byte{0xCC});std::size_t failed=99;
        fixture::check(!e::consume(s,scratch,incoming,std::span(output).first(capacity),failed)
            && failed==0 && s.sendNonce==before.sendNonce
            && q::staging::same_state(s.queuez,before.queuez)
            && fixture::commits==0 && fixture::discards==1
            && std::all_of(output.begin(),output.end(),[](std::byte byte){return byte==std::byte{0xCC};}),"capacity failure atomic discard");
    }
    fixture::reset();s=session();incoming=request(s,service);
    fixture::check(e::consume(s,scratch,incoming,output,written) && s.queuez.family3Version==8,"discarded response retries same nextversion");
    incoming=request(s,service);
    fixture::check(e::consume(s,scratch,incoming,output,written) && s.queuez.family3Version==9,"ordered committed retry nextversion");
}
}
int main(int argc,char** argv) {
    const bool requested=argc>1 && argv[1][0]=='1';
    const bool enabled=true;
    fixture::check(SetEnvironmentVariableW(L"DAWN_INLINE_SUBSCRIPTION_UPDATES",requested ? L"1" : nullptr)!=0,"fixture process flag");
    successful(10,enabled);successful(110,enabled);
    for(const auto service:{std::uint16_t{10},std::uint16_t{110}}) {
        for(int failure=0;failure<4;++failure) {
            if(failure==2) continue;
            fixture::reset();auto s=session();const auto before=s;auto incoming=request(s,service);
            if(failure==0)fixture::active=false;
            if(failure==1)s.authenticated=false;
            if(failure==2)fixture::stageOkay=false;
            if(failure==3)fixture::commitOkay=false;
            std::size_t written=99;
            fixture::check(!e::consume(s,scratch,incoming,output,written) && written==0
                && s.sendNonce==before.sendNonce && q::staging::same_state(s.queuez,before.queuez)
                && fixture::publishes==0,"auth/stage/commit failure retains state");
            fixture::check(failure>=2 || (fixture::bodyCalls==0 && s.receiveNonce==before.receiveNonce),"unauth/expired never reach body/store");
        }
        fixture::reset();auto invalidSession=session();const auto invalidBefore=invalidSession;
        auto invalid=request(invalidSession,service);sealedBytes[0]^=std::byte{1};std::size_t invalidSize=99;
        fixture::check(!e::consume(invalidSession,scratch,invalid,output,invalidSize)
            && invalidSize==0 && fixture::bodyCalls==0 && fixture::commits==0
            && invalidSession.receiveNonce==invalidBefore.receiveNonce
            && invalidSession.sendNonce==invalidBefore.sendNonce,"bad authenticated tag never reaches store or consumes nonce");
        for(int mode=0;mode<5;++mode) {
            fixture::reset();auto s=session();const auto before=s;
            auto incoming=request(s,service,206,mode==1);
            if(mode==0)fixture::bodyOkay=false;
            if(mode==2)fixture::suppress=true;
            if(mode==3) { fixture::stageOkay=false; s.activityRosterStaged.staged=true; }
            if(mode==4) { fixture::replacementFailure=true; s.activityRosterStaged.staged=true; }
            std::size_t written{};
            fixture::check(e::consume(s,scratch,incoming,output,written)
                && q::staging::same_state(s.queuez,before.queuez),"refusal emits only failure and preserves snapshot state");
            fixture::check(fixture::commits==0&&fixture::publishes==0&&fixture::rosterCommits==0&&!s.activityRosterStaged.staged&&fixture::discards>0,"failed snapshot discards roster grants and commits no business or connection state");
            std::size_t next{};auto response=open(before,0,before.sendNonce,next);
            fixture::check(response.size()==8&&next==written
                && m::encoding::read_u16_be(response.first<2>())==(service==110 ? 112 : 11)
                && m::encoding::read_u32_be(response.subspan<2,4>())==task
                && m::encoding::read_u16_be(response.subspan<6,2>())==(mode<2 ? 400 : 503),
                "correlated outer refusal has no WS statusOnly or invented source");
            auto expected=before.sendNonce;m::secure_channel::advance_nonce(expected);
            fixture::check(s.sendNonce==expected,"only actual correlated error consumes send nonce");
            for(std::size_t capacity=0;capacity<written;++capacity) {
                fixture::reset();auto shortSession=session();fixture::suppress=true;
                auto shortRequest=request(shortSession,service);output.fill(std::byte{0xCC});std::size_t count=99;
                fixture::check(!e::consume(shortSession,scratch,shortRequest,std::span(output).first(capacity),count)
                    && count==0&&shortSession.sendNonce==before.sendNonce&&fixture::commits==0
                    && q::staging::same_state(shortSession.queuez,before.queuez)
                    && std::all_of(output.begin(),output.end(),[](auto byte){return byte==std::byte{0xCC};}),
                    "refusal capacity failure preserves output nonce and state");
            }
        }
        for(const auto shape:{ws::ResponseShape::statusOnly,ws::ResponseShape::statusPair,ws::ResponseShape::statusPairWithBool}) {
            const std::uint16_t opcode=shape==ws::ResponseShape::statusOnly ? 304
                : shape==ws::ResponseShape::statusPair ? 403 : 901;
            fixture::reset();auto s=session();const auto before=s;
            auto incoming=request(s,service,opcode);std::size_t written{};
            fixture::check(e::consume(s,scratch,incoming,output,written) && !fixture::sawInline,"non206 mutation/error route not widened");
            std::size_t next{};auto response=open(before,0,before.sendNonce,next);
            std::array<std::byte,64> expected{};std::size_t count{};
            const ws::Message original{opcode,transaction,{}};
            fixture::check(ws::encode_response(original,shape,fixture::status,expected,count)
                && response.size()==8+count && std::equal(expected.begin(),expected.begin()+count,response.begin()+8),"all status/int/bool response bits identical");
            ws::Message parsed{};std::uint64_t scalar{};
            fixture::check(ws::parse_request(response.subspan(8),parsed),"reflected non206 header");
            m::encoding::bits::Reader fields(parsed.payload);
            fixture::check(fields.read(5,scalar) && scalar==8,"original nonzero status bias");
            if(shape!=ws::ResponseShape::statusOnly)
                fixture::check(fields.read(32,scalar) && scalar==0x81234567,"original signed integer bias");
            if(shape==ws::ResponseShape::statusPairWithBool)
                fixture::check(fields.read(1,scalar) && scalar==1,"original required bool");
            fixture::check(fields.read(2,scalar) && scalar==0,"original non206 optionals unchanged");
        }
    }
    std::printf("PASS %d: actual consume svc11/112, real AES/reflected fields, same prepared progress, nonce/companion order, discard/retry and unchanged errors/mutations (inline=%d)\n",fixture::checks,enabled);
}
