#include <cstdio>
#include <cstdlib>
#include <vector>
#include "fixtures/profile_setup_wire.h"
#include "../restorationserver/src/account_codec.h"
#include "middleware/web_service/messages/opcode701.h"
#include "middleware/datagen/family4/account/account_encoder.h"
#include "middleware/datagen/family4/account/layout.h"
#include "middleware/datagen/family4/loadout/loadout_resolver.h"
#include "state/account/settings/settings_runtime.h"
#include "state/runtime/storage/internal.h"
#include "state/build_data/runtime.h"

namespace {
using Account=dawn::state::AccountState;
Account current;
std::vector<std::byte> durable;
bool rejectStorage{};
int buildDataCalls{};
void check(bool value,const char* label) {
    if(!value) { std::printf("FAIL: %s\n",label);std::exit(1); }
}
}
namespace dawn::state::build_data {
bool find_item_definition_hash(std::uint32_t,items::Definition&) noexcept { ++buildDataCalls;return false; }
bool find_configured_item_detail(std::uint16_t,items::details::Definition&) noexcept { ++buildDataCalls;return false; }
bool is_profile_action_source(std::uint16_t,std::uint8_t) noexcept { ++buildDataCalls;return false; }
bool find_inventory_bucket_descriptor(std::uint8_t,inventory::buckets::Descriptor&) noexcept { ++buildDataCalls;return false; }
bool find_progression_slots(progressions::Scope,std::span<std::uint16_t>,std::size_t& count) noexcept {
    count=0;return true;
}
}
namespace dawn::middleware::datagen::family4::loadout {
bool resolve_instances(const state::AccountState&,std::size_t,ResolvedInstances&) noexcept {
    ++buildDataCalls;return false;
}
}
namespace dawn::state::runtime::storage {
SRWLOCK g_stateLock{SRWLOCK_INIT};
AccountState& account() noexcept { return current; }
bool publish_account_locked(const AccountState& candidate) noexcept {
    if(rejectStorage || !restoration::account_codec::encode(candidate,durable))return false;
    current=candidate;return true;
}
}
int main() {
    namespace state=dawn::state;
    namespace ws=dawn::middleware::web_service;
    namespace codec=restoration::account_codec;
    namespace family4=dawn::middleware::datagen::family4::account;
    current.primarySoid=0x9EAA000100200100ULL;
    auto& settings=current.settings;
    settings.configured=true;settings.keyBindings.configured=true;
    settings.controls.mouseLookSensitivity=11;settings.controls.adsSensitivityModifier=1.0F;
    settings.audio.migrationVersion=state::account::settings::kCompletedAudioMigrationVersion;
    settings.display.calibrationPrimary=10000.0F;settings.display.fieldOfView=105;
    check(state::account::valid(current),"actual canonical account validation");
    const auto before=settings;
    check(!before.profileSetupCompleted,"first setup remains incomplete");
    const auto parse=[&](std::span<const std::byte> bytes,auto& output) {
        ws::Message message{};message.opcode=701;message.payload=bytes;
        return ws::messages::opcode701::parse_settings(message,output,output);
    };
    auto after=before;
    check(parse(settings_wire_fixture::setup_complete,after) && after.profileSetupCompleted,
          "native optional true saves setup confirmation");
    auto onlySetup=after;onlySetup.profileSetupCompleted=false;
    check(onlySetup==before,"setup-only report preserves every other setting");
    check(parse(settings_wire_fixture::setup_absent,after) && after.profileSetupCompleted,
          "absent optional flag retains completion");
    auto explicitFalse=after;
    check(parse(settings_wire_fixture::setup_incomplete,explicitFalse) && !explicitFalse.profileSetupCompleted,
          "explicit false uses native bool semantics");
    for(std::size_t n=0;n<settings_wire_fixture::setup_complete.size();++n) {
        auto unchanged=before;
        check(!parse(std::span(settings_wire_fixture::setup_complete).first(n),unchanged)
              && unchanged==before,"truncated packet cannot publish flag");
    }
    bool changed{};
    rejectStorage=true;
    check(!state::account::settings::save(current.primarySoid,before,after,changed)
          && !changed && settings==before,"storage refusal leaves native completion unpublished");
    rejectStorage=false;
    check(!state::account::settings::save(current.primarySoid+1,before,after,changed),
          "foreign account rejected");
    check(state::account::settings::save(current.primarySoid,before,after,changed) && changed,
          "authenticated compare/commit writes actual version 7 payload");
    check(!state::account::settings::save(current.primarySoid,before,explicitFalse,changed),
          "stale account before-image rejected");
    static Account reloaded;
    check(codec::decode(durable,reloaded) && reloaded.settings==after,"actual codec reload retains completion");
    std::vector<std::byte> image(family4::layout::kMinimumSize),incomplete(image.size());
    check(family4::encode(reloaded,image) && image[0xB90]==std::byte{1},
          "actual Family4 encoder restores native setup gate B90");
    reloaded.settings.profileSetupCompleted=false;
    check(family4::encode(reloaded,incomplete) && incomplete[0xB90]==std::byte{0},"first-time Family4 gate zero");
    std::size_t differences{};
    for(std::size_t i=0;i<image.size();++i) differences+=image[i]!=incomplete[i];
    check(differences==1,"completion changes only the proved native byte");
    reloaded.settings=after;
    for(int version:{codec::kPreSetupVersion,codec::kPreFovVersion,codec::kPreInputVersion,codec::kPriorVersion}) {
        std::vector<std::byte> legacy;static Account migrated;
        check(codec::encode(reloaded,legacy,version) && codec::decode(legacy,migrated,version)
              && !migrated.settings.profileSetupCompleted,"legacy migration never invents setup completion");
        check(migrated.settings.display.fieldOfView==(version>=6?105:85),"version 6 FOV preserved");
    }
    check(buildDataCalls==0,"no substituted item/build-data behavior was exercised");
    std::puts("PASS: native setup reports, exact B90 publication, durable codec reload, storage/auth fences and v1/v4/v5/v6 migration");
}
