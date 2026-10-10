#pragma once
#include "tower_configuration_self_test.h"
#include "state/matchmaking/social_search.h"
#include "state/matchmaking/matchmaking_state.h"
#include "middleware/bap/matchmaking/social_search.h"
#include "middleware/bap/matchmaking/response/matchmaking_response_encoder.h"

namespace restoration::tests {
inline bool social_search_self_test(const dawn::state::AccountRuntimeContext& target,
    const dawn::state::AccountRuntimeContext& caller,std::uint64_t host,
    std::uint32_t tower) noexcept {
    if (!tower_configuration_self_test()) return false;
    using namespace dawn;namespace mm=state::matchmaking;
    namespace wire=middleware::bap::matchmaking;namespace a=state::activity;
    const auto check=[](bool ok,const char* step){if(!ok)std::printf("FAIL: social search: %s\n",step);return ok;};
    constexpr std::array<std::uint64_t,2> platforms{0x01100001F0000004ULL,0x01100001F0000005ULL};
    {
        std::array<std::byte,64> bytes{};middleware::protobuf::Writer writer(bytes);
        if(!writer.write_varint(4,0) || !writer.write_varint(5,tower))return false;
        auto parsed=wire::social_search::scope(std::span(bytes).first(writer.size()));
        if(!check(parsed.valid && parsed.primary && parsed.activity==tower,"wire type zero is primary"))return false;
        if(!writer.write_varint(4,1) || !check(!wire::social_search::scope(std::span(bytes).first(writer.size())).valid,
            "duplicate/wrong-scope field fails closed"))return false;
        bytes={};middleware::protobuf::Writer publicWriter(bytes);
        if(!publicWriter.write_varint(4,1) || !publicWriter.write_varint(5,tower))return false;
        parsed=wire::social_search::scope(std::span(bytes).first(publicWriter.size()));
        if(!check(parsed.valid && !parsed.primary,"PUBLIC target is not primary"))return false;
        if(!check(!wire::social_search::scope(std::span(bytes).first(publicWriter.size()-1)).valid,
            "truncated scope rejected"))return false;
    }
    std::array<mm::ContextHandle,2> handles{};
    std::array<std::array<std::byte,128>,2> descriptors{};
    for(std::size_t i=0;i<2;++i) {
        if(!mm::acquire_context(handles[i]))return false;
        auto& d=descriptors[i];const auto machine=std::uint64_t{0xAB1000}+i,session=std::uint64_t{0xBA2000}+i;
        std::memcpy(d.data(),&machine,8);std::memcpy(d.data()+110,&session,8);
        const auto text=std::string("steamid:")+std::to_string(platforms[i])+"#11223344";
        std::memcpy(d.data()+8,text.data(),text.size());d[93]=std::byte{6};d[94]=std::byte{0xA1};
    }
    a::SessionRecord old{};
    if(!a::snapshot_for_runtime(target,target.account->primarySoid,host,old))return false;
    {
        const state::ScopedAccountRuntime scope(target);
        a::membership::AuthoritativeUpdate update{};a::membership::PendingMutation plan{};
        update.peerConnection=old.membership.peerConnection;
        std::copy_n(descriptors[0].begin()+8,86,update.peerConnection.peer.begin());
        if(!a::membership::prepare_authoritative(host,update,plan) || !a::membership::commit(plan))return false;
    }
    const auto advertise=[&](std::size_t i,std::uint32_t activity,bool primary=true) {
        const auto& context=i==0?target:caller;const state::ScopedAccountRuntime scope(context);
        mm::PendingMutation mutation{};std::uint64_t id{};
        if(!mm::prepare_variant_update(handles[i],0,3,true,descriptors[i],id,mutation))return false;
        mutation.socialAccount=context.account->primarySoid;mutation.socialPlatform=platforms[i];
        mutation.socialActivity=activity;mutation.socialPrimary=primary
            && wire::social_search::steam_descriptor(descriptors[i],platforms[i]);
        mutation.socialParty[0]=context.account->primarySoid;
        return mm::commit(mutation);
    };
    if(!advertise(0,tower) || !advertise(1,tower))return false;
    {
        const state::ScopedAccountRuntime scope(caller);
        mm::LatestSnapshot result{};mm::PendingMutation grant{};
        if(!check(mm::prepare_social_search(handles[1],tower,result,grant) && result.descriptor==descriptors[0],
            "independent primary search returns exact native live Tower descriptor"))return false;
        if(!check(!mm::social_search_peers(caller.account->primarySoid,target.account->primarySoid),
            "uncommitted reply does not grant signaling"))return false;
        std::array<std::byte,160> body{};body.fill(std::byte{0x77});const auto before=body;std::size_t written=99;
        wire::Response response{};response.kind=wire::RequestKind::sessionSearch;response.descriptor=result.descriptor;
        if(!check(!wire::response::encode(response,std::span(body).first(130),written) && written==0 && body==before,
            "short reply is atomic"))return false;
        if(!wire::response::encode(response,body,written))return false;
        {
            middleware::protobuf::Reader outer(std::span(body).first(written));
            middleware::protobuf::Field root{},entry{},limit{};
            if(!check(outer.next(root) && outer.remaining()==0 && root.fieldNumber==3
                && root.wireType==middleware::protobuf::WireType::lengthDelimited,
                "search response envelope"))return false;
            middleware::protobuf::Reader results(root.bytes);
            if(!check(results.next(entry) && entry.fieldNumber==1
                && entry.wireType==middleware::protobuf::WireType::lengthDelimited
                && results.next(limit) && limit.fieldNumber==2
                && limit.wireType==middleware::protobuf::WireType::varint
                && limit.value==200 && results.remaining()==0,
                "native result latency ceiling present, finite, and distinct from ping estimates"))return false;
        }
        std::span<const std::byte> nested=std::span(body).first(written);
        std::size_t depth=0;
        for(const unsigned tag:std::array<unsigned,4>{3,1,1,1}) {
            middleware::protobuf::Reader reader(nested);middleware::protobuf::Field field{};
            if(!check(reader.next(field) && (depth==1 || reader.remaining()==0) && field.fieldNumber==tag
                && field.wireType==middleware::protobuf::WireType::lengthDelimited,"native nested search-result schema"))return false;
            nested=field.bytes;
            ++depth;
        }
        if(!check(nested.size()==128 && std::equal(nested.begin(),nested.end(),descriptors[0].begin()),
            "descriptor secret/address/session bytes preserved exactly"))return false;
        body.fill(std::byte{0x77});written=99;
        if(!check(!wire::response::encode(response,std::span(body).first(142),written)
            && written==0 && body==before,"new latency field short buffer remains atomic"))return false;
        response.descriptor={};
        if(!check(wire::response::encode(response,body,written) && written==2
            && body[0]==std::byte{0x1A} && body[1]==std::byte{0},
            "empty search keeps legacy response bytes"))return false;
        std::puts("PASS: social search positive native latency ceiling; exact varint schema, descriptor preservation, empty-response compatibility and atomic short buffer");
        auto bad=grant;bad.socialGrant.generation++;
        if(!check(!mm::commit(bad),"wrong world generation denied"))return false;
        bad=grant;bad.socialGrant.partyAccounts[1]=0xDEAD;
        if(!check(!mm::commit(bad),"forged party extension denied"))return false;
        bad=grant;bad.socialGrant.descriptor[94]^=std::byte{1};
        if(!check(!mm::commit(bad),"changed native secret denied"))return false;
        bad=grant;bad.socialGrant.expires=GetTickCount64()-1;
        if(!check(!mm::commit(bad),"expired search denied"))return false;
        if(!check(mm::commit(grant)
            && mm::social_search_peers(caller.account->primarySoid,target.account->primarySoid,host)
            && mm::social_search_peers(target.account->primarySoid,caller.account->primarySoid,host)
            && !mm::social_search_peers(caller.account->primarySoid,target.account->primarySoid,host+1)
            && !mm::social_search_peers(0xDEAD,target.account->primarySoid,host),"bounded bidirectional grant, wrong activity and outsider denied"))return false;
    }
    descriptors[0][94]^=std::byte{1};if(!advertise(0,tower))return false;
    if(!check(!mm::social_search_peers(caller.account->primarySoid,target.account->primarySoid),"rekey revokes old grant"))return false;
    // RUN91: PC host-reestablish can advertise a full peer descriptor with no platform lobby;
    // do not invent a lobby or drop the new session/address/key for the old advertisement
    {
        const std::uint64_t noPlatformLobby=0;
        std::memcpy(descriptors[0].data()+110,&noPlatformLobby,8);
        descriptors[0][0]^=std::byte{0x40};
        if(!check(wire::social_search::steam_descriptor(descriptors[0],platforms[0]),
            "RUN91 native migrated descriptor without platform lobby accepted"))return false;
        auto invalid=descriptors[0];invalid[93]=std::byte{0};
        if(!check(!wire::social_search::steam_descriptor(invalid,platforms[0])
            && !wire::social_search::steam_descriptor(descriptors[0],platforms[1]),
            "RUN91 optional lobby does not relax transport or authenticated owner"))return false;
        invalid=descriptors[0];invalid[127]=std::byte{1};
        if(!check(!wire::social_search::steam_descriptor(invalid,platforms[0]),
            "RUN91 partially present platform block with zero lobby rejected"))return false;
        std::fill_n(invalid.begin(),8,std::byte{});invalid[93]=std::byte{6};
        if(!check(!wire::social_search::steam_descriptor(invalid,platforms[0]),
            "RUN91 absent native session still rejected"))return false;
        if(!advertise(0,tower))return false;
        const state::ScopedAccountRuntime scope(caller);
        mm::LatestSnapshot result{};mm::PendingMutation grant{};
        if(!check(mm::prepare_social_search(handles[1],tower,result,grant)
            && result.descriptor==descriptors[0] && mm::commit(grant)
            && mm::social_search_peers(caller.account->primarySoid,target.account->primarySoid,host)
            && mm::social_search_peers(target.account->primarySoid,caller.account->primarySoid,host)
            && !mm::social_search_native_lobby(caller.account->primarySoid,target.account->primarySoid,platforms[0],0)
            && !mm::social_search_native_lobby(caller.account->primarySoid,target.account->primarySoid,platforms[0],0xBA2000),
            "RUN91 zero-lobby native search grants exact peer path, never synthetic Steam admission"))return false;
        std::array<std::byte,160> reply{};std::size_t count{};
        if(!check(wire::social_search::encode(result.descriptor,reply,count) && count>128
            && std::search(reply.begin(),reply.begin()+count,descriptors[0].begin(),descriptors[0].end())!=reply.begin()+count,
            "RUN91 returned migrated descriptor preserved byte for byte"))return false;
        std::memcpy(descriptors[1].data()+110,&noPlatformLobby,8);
        if(!advertise(1,tower))return false;
        if(!check(mm::prepare_social_search(handles[1],tower,result,grant) && mm::commit(grant),
            "RUN91 distinct native sessions with both platform IDs absent still match"))return false;
        const auto own=descriptors[1];descriptors[1]=descriptors[0];
        mm::PendingMutation self{};std::uint64_t id{};
        if(!mm::prepare_variant_update(handles[1],0,3,true,descriptors[1],id,self))return false;
        self.socialAccount=caller.account->primarySoid;self.socialPlatform=platforms[1];
        self.socialActivity=tower;self.socialPrimary=true;self.socialParty[0]=caller.account->primarySoid;
        if(!mm::commit(self) || !check(!mm::prepare_social_search(handles[1],tower,result,grant),
            "RUN91 identical native endpoint/session is a self result even without platform lobby"))return false;
        descriptors[1]=own;if(!advertise(1,tower))return false;
        descriptors[0][8+30]^=std::byte{1};if(!advertise(0,tower))return false;
        if(!check(!mm::prepare_social_search(handles[1],tower,result,grant),
            "RUN91 migrated descriptor must still match live authenticated world endpoint"))return false;
        descriptors[0][8+30]^=std::byte{1};if(!advertise(0,tower))return false;
        std::puts("PASS: RUN91 migrated native social search: optional platform lobby, exact bytes, native self exclusion and peer authorization guards");
    }
    if(!advertise(0,tower^1U) || !advertise(1,tower^1U))return false;
    {
        const state::ScopedAccountRuntime scope(caller);mm::LatestSnapshot result{};mm::PendingMutation grant{};
        if(!check(!mm::prepare_social_search(handles[1],tower^1U,result,grant),
            "RUN94 matching advertisements cannot cross the live definition"))return false;
    }
    if(!advertise(0,tower))return false;
    if(!advertise(1,tower+1))return false;
    {
        const state::ScopedAccountRuntime scope(caller);mm::LatestSnapshot result{};mm::PendingMutation grant{};
        if(!check(!mm::prepare_social_search(handles[1],tower+1,result,grant),"different activity excluded"))return false;
    }
    if(!advertise(1,tower,false))return false;
    {
        const state::ScopedAccountRuntime scope(caller);mm::LatestSnapshot result{};mm::PendingMutation grant{};
        if(!check(!mm::prepare_social_search(handles[1],tower,result,grant),"derived PUBLIC path unchanged"))return false;
    }
    if(!advertise(1,tower))return false;
    auto stale=handles[0];if(!mm::release_context(handles[0]) || !mm::acquire_context(handles[0]))return false;
    {
        const state::ScopedAccountRuntime scope(caller);mm::LatestSnapshot result{};mm::PendingMutation grant{};
        if(!check(!mm::prepare_social_search(handles[1],tower,result,grant) && !mm::latest_snapshot(stale,result),
            "released/reused context cannot advertise stale session"))return false;
    }
    for(auto handle:handles)if(!mm::release_context(handle))return false;
    {
        const state::ScopedAccountRuntime scope(target);a::membership::AuthoritativeUpdate update{};
        update.peerConnection=old.membership.peerConnection;a::membership::PendingMutation plan{};
        if(!a::membership::prepare_authoritative(host,update,plan) || !a::membership::commit(plan))return false;
    }
    std::puts("PASS: social search native descriptor, atomic reply, generation/identity/rekey/expiry/outsider guards and PUBLIC exclusion");
    return true;
}
}
