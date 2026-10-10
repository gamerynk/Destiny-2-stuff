#include "../dawn/src/server/gameplay/group/public_membership_binding.h"
#include <array>
#include "../dawn/src/state/activity/shared/public_member_connection_policy.h"
#include <cstdio>
struct Record {bool occupied{};std::uint64_t sessionId{},joiningPlatformId{},activityMemberKey{},activityAccount{},activitySource{},activityCreatedRevision{},activityJoinedRevision{},activityPlayerId{};};
int main(){
 namespace policy=dawn::server::gameplay::group::public_membership_binding;
 int checks=0,failed=0;const auto check=[&](bool value){++checks;if(!value)++failed;};
 const Record host{true,11,101,501,201,31,1,2,1001},guest{true,11,102,502,202,31,1,2,1002};
 Record claimed{true,11,103};
 check(policy::ready(host));check(policy::ready(guest));check(!policy::ready(claimed));
 std::array<Record,3> rows{host,guest,claimed};unsigned published=0;
 for(const auto& row:rows)if(policy::ready(row))++published;
 check(published==2);check(!policy::ready(rows[2]));
 rows[2]={true,11,103,503,203,31,1,2,1003};published=0;
 for(const auto& row:rows)if(policy::ready(row))++published;
 check(published==3);check(rows[2].activityMemberKey!=host.activityMemberKey);
 const auto missing=[&](Record r){check(!policy::ready(r));};
 auto r=guest;r.occupied=false;missing(r);r=guest;r.sessionId=0;missing(r);
 r=guest;r.joiningPlatformId=0;missing(r);r=guest;r.activityMemberKey=0;missing(r);
 r=guest;r.activityAccount=0;missing(r);r=guest;r.activitySource=0;missing(r);
 r=guest;r.activityCreatedRevision=0;missing(r);r=guest;r.activityJoinedRevision=0;missing(r);
 const policy::SourceFence source{202,31,502,1,2,1002,true};
 check(policy::current(guest,source));
 auto fence=source;fence.valid=false;check(!policy::current(guest,fence));
 fence=source;fence.account++;check(!policy::current(guest,fence));
 fence=source;fence.source++;check(!policy::current(guest,fence));
 fence=source;fence.member++;check(!policy::current(guest,fence));
 fence=source;fence.created++;check(!policy::current(guest,fence));
 fence=source;fence.joined++;check(!policy::current(guest,fence));
 fence=source;fence.player++;check(!policy::current(guest,fence));
 r=guest;r.activityPlayerId=0;check(!policy::current(r,source));
 r=guest;r.activityPlayerId=UINT64_MAX;check(!policy::current(r,source));
 namespace shared=dawn::state::activity::shared;
 struct Binding {std::uint64_t account{},source{},sourceCreated{},sourceJoined{};};
 const Binding binding{202,31,1,2};const shared::PublicMemberConnectionFence expected{202,31,1,2};
 check(shared::public_connection_fence_matches(binding,expected));
 auto replaced=binding;replaced.sourceCreated++;check(!shared::public_connection_fence_matches(replaced,expected));
 replaced=binding;replaced.sourceJoined++;check(!shared::public_connection_fence_matches(replaced,expected));
 replaced=binding;replaced.source++;check(!shared::public_connection_fence_matches(replaced,expected));
 replaced=binding;replaced.account++;check(!shared::public_connection_fence_matches(replaced,expected));
 dawn::state::activity::SessionRecord participant{};participant.joined=true;
 participant.sharedOwnerCreatedRevision=44;participant.sharedMemberSlot=3;participant.membership.hasIdentity=true;
 check(shared::retained_public_connection_allowed(nullptr,&participant,44,3,true));
 dawn::state::activity::SessionRecord privateRow{};privateRow.joined=false;
 check(shared::retained_public_connection_allowed(&privateRow,&participant,44,3,true));
 privateRow.joined=true;check(!shared::retained_public_connection_allowed(&privateRow,&participant,44,3,true));
 check(!shared::retained_public_connection_allowed(nullptr,nullptr,44,3,true));
 check(!shared::retained_public_connection_allowed(nullptr,&participant,45,3,true));
 check(!shared::retained_public_connection_allowed(nullptr,&participant,44,4,true));
 check(!shared::retained_public_connection_allowed(nullptr,&participant,44,3,false));
 participant.sharedOwnerDetached=true;check(!shared::retained_public_connection_allowed(nullptr,&participant,44,3,true));participant.sharedOwnerDetached=false;
 participant.restoredDormant=true;check(!shared::retained_public_connection_allowed(nullptr,&participant,44,3,true));participant.restoredDormant=false;
 participant.joined=false;check(!shared::retained_public_connection_allowed(nullptr,&participant,44,3,true));participant.joined=true;
 participant.membership.hasIdentity=false;check(!shared::retained_public_connection_allowed(nullptr,&participant,44,3,true));
 std::printf("public membership binding: %d checks, %d failures\n",checks,failed);return failed?1:0;
}
