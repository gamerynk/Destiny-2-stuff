#pragma once
#include <cstdio>
#include <vector>
#include "fixtures/account_settings_wire.h"
#include "fixtures/fov_settings_wire.h"
#include "fixtures/profile_setup_wire.h"
#include "middleware/datagen/family4/account/account_encoder.h"
#include "middleware/datagen/family4/account/layout.h"
#include "../restorationserver/src/account_codec.h"
#include "../restorationserver/src/account_store.h"
#include "middleware/web_service/messages/opcode701.h"
#include "middleware/datagen/family4/account/preferences/preferences_encoder.h"
#include "server/web_service/settings_save.h"
#include "state/runtime/runtime.h"
#include "state/runtime/storage/internal.h"

namespace restoration::tests {
inline bool input_settings_self_test(void* module, const dawn::state::AccountState& seed) noexcept {
    namespace state=dawn::state;
    namespace ws=dawn::middleware::web_service;
    namespace codec=ws::messages::opcode701;
    const auto check=[](bool ok,const char* label) noexcept {
        if(!ok) std::printf("FAIL: input settings: %s\n",label);return ok;
    };
    const auto message=[](std::span<const std::byte> bytes) noexcept {
        ws::Message m{};m.opcode=701;m.payload=bytes;return m;
    };
    struct Cleanup { ~Cleanup() { account_store::shutdown(); } } cleanup;
    constexpr std::uint64_t platform=0x0110000100F00201ULL;
    account_store::LoadResult loaded{};
    if(!check(account_store::initialize(module,seed,"Settings-Owner",platform,loaded),"initialize"))return false;
    const auto baseline=loaded.account;
    const auto before=loaded.account.settings;
    if(!check(!before.profileSetupCompleted,"new account requires actual setup confirmation"))return false;
    auto decoded=before;
    if(!check(codec::parse_settings(message(settings_wire_fixture::full),before,decoded)
        && decoded.keyBindings.seeded && decoded.display.fieldOfView==85,"native full report"))return false;
    auto fov = decoded;
    if(!check(codec::parse_settings(message(settings_wire_fixture::fov_105),decoded,fov)
        && fov.display.fieldOfView==105 && fov.controls==decoded.controls
        && fov.keyBindings==decoded.keyBindings,"sparse FOV update preserves inputs"))return false;
    if(!check(codec::parse_settings(message(settings_wire_fixture::partial),fov,decoded)
        && decoded.display.fieldOfView==105,"absent FOV preserves saved value"))return false;
    if(!check(!codec::parse_settings(message(settings_wire_fixture::invalid_fov),fov,decoded),
        "FOV below native range rejected"))return false;
    if(!check(codec::parse_settings(message(settings_wire_fixture::unseeded_fov),fov,decoded)
        && decoded.display.fieldOfView==105,"unseeded zero FOV cannot overwrite preference"))return false;
    if(!check(codec::parse_settings(message(settings_wire_fixture::fov_55),fov,decoded)
        && decoded.display.fieldOfView==55,"minimum FOV is a valid saved choice"))return false;
    for(std::size_t n=0;n<settings_wire_fixture::full.size();++n) {
        auto untouched=before;
        if(!check(!codec::parse_settings(message(std::span(settings_wire_fixture::full).first(n)),before,untouched)
            && untouched==before,"all truncations leave settings unchanged"))return false;
    }
    auto tail=std::vector<std::byte>(settings_wire_fixture::full.begin(),settings_wire_fixture::full.end());
    tail.push_back(std::byte{0});
    if(!check(!codec::parse_settings(message(tail),before,decoded),"trailing bytes"))return false;
    state::activity::ActivityState activity{};
    state::AccountRuntimeContext context{&loaded.account,&activity,
        {nullptr,&account_store::save_callback},{}};
    const state::ScopedAccountRuntime scope(context);
    std::array<std::byte,64> response{};std::size_t written{};
    if(!check(!dawn::server::web_service::save_settings(message(settings_wire_fixture::full),
        std::span(response).first(1),written) && loaded.account.settings==before,"output capacity before commit"))return false;
    const auto save=[&](std::span<const std::byte> bytes) noexcept {
        return dawn::server::web_service::save_settings(message(bytes),response,written);
    };
    if(!check(save(settings_wire_fixture::full) && save(settings_wire_fixture::partial)
        && save(settings_wire_fixture::fov_105) && save(settings_wire_fixture::setup_complete)
        && save(settings_wire_fixture::setup_absent),"durable native reports"))return false;
    const auto saved=loaded.account.settings;
    if(!check(saved.controls.mouseLookSensitivity==17 && saved.controls.adsSensitivityModifier==1.25F
        && saved.keyBindings.seeded && saved.keyBindings.useLocalSource
        && saved.display.fieldOfView==105 && saved.profileSetupCompleted,
        "changed input values, FOV and native setup confirmation"))return false;
    const auto& jump=saved.keyBindings.values[static_cast<std::size_t>(state::account::settings::bindings::Action::jump)];
    if(!check(jump.primary==0x39 && jump.secondary==0x6f,"changed key binding"))return false;
    auto original=loaded.account;original.settings=before;
    std::vector<std::byte> currentBytes,beforeBytes;
    if(!check(account_codec::encode(original,currentBytes) && account_codec::encode(baseline,beforeBytes)
        && currentBytes==beforeBytes,"unchanged account and inventory"))return false;
    const auto unchanged=loaded.account.settings;
    if(!check(save(settings_wire_fixture::invalid_mouse) && loaded.account.settings==unchanged,"invalid input rejected"))return false;
    bool changed{};
    if(!check(!state::account::settings::save(loaded.account.primarySoid+1,saved,before,changed)
        && !state::account::settings::save(loaded.account.primarySoid,before,saved,changed),"account and stale-write fences"))return false;
    {
        state::AccountRuntimeContext failing{&loaded.account,&activity,
            {nullptr, +[](void*, const state::AccountState&) noexcept { return false; }},{}};
        const state::ScopedAccountRuntime failScope(failing);
        if(!check(!state::account::settings::save(loaded.account.primarySoid,saved,before,changed)
            && !changed && loaded.account.settings==saved,"failed storage does not publish settings"))return false;
    }
    std::array<std::byte,72> deviceKey{};deviceKey.fill(std::byte{0x29});
    account_store::DeviceBinding guest{};
    constexpr std::uint64_t guestPlatform=platform+1;
    if(!check(account_store::load_or_create_device(0xF00202,guestPlatform,deviceKey,guest),"independent player account"))return false;
    {
        const state::ScopedAccountRuntime guestScope(guest.runtime);
        const auto guestBefore=state::account_snapshot().settings;
        auto guestAfter=guestBefore;guestAfter.controls.mouseLookSensitivity=19;
        if(!check(state::account::settings::save(guest.accountSoid,guestBefore,guestAfter,changed)
            && changed && loaded.account.settings==saved,"guest writes use its own persistence callback"))return false;
    }
    account_store::shutdown();account_store::LoadResult reopened{};
    if(!check(account_store::initialize(module,seed,"Settings-Owner",platform,reopened)
        && reopened.account.settings==saved,"durable reopen survives authored defaults"))return false;
    state::AccountState guestReloaded{};std::array<char,account_store::kAccountNameCapacity> guestName{};
    std::uint64_t guestRevision{};
    if(!check(account_store::profile_by_platform(guestPlatform,guestReloaded,guestRevision,guestName)
        && guestReloaded.settings.controls.mouseLookSensitivity==19
        && reopened.account.settings.controls.mouseLookSensitivity==17,"two accounts reopen independently"))return false;
    std::vector<std::byte> legacy;state::AccountState migrated{};
    if(!check(account_codec::encode(reopened.account,legacy,account_codec::kPreInputVersion)
        && account_codec::decode(legacy,migrated,account_codec::kPreInputVersion)
        && migrated.settings.controls==saved.controls && !migrated.settings.keyBindings.seeded
        && migrated.settings.keyBindings.values==saved.keyBindings.values,"version 4 migration preserves old preferences"))return false;
    if(!check(account_codec::encode(reopened.account,legacy,account_codec::kPreFovVersion)
        && account_codec::decode(legacy,migrated,account_codec::kPreFovVersion)
        && migrated.settings.controls==saved.controls && migrated.settings.keyBindings==saved.keyBindings
        && migrated.settings.display.fieldOfView==85,"version 5 migration preserves inputs and uses native FOV default"))return false;
    if(!check(account_codec::encode(reopened.account,legacy,account_codec::kPreSetupVersion)
        && account_codec::decode(legacy,migrated,account_codec::kPreSetupVersion)
        && migrated.settings.controls==saved.controls && migrated.settings.keyBindings==saved.keyBindings
        && migrated.settings.display.fieldOfView==105 && !migrated.settings.profileSetupCompleted,
        "version 6 migration retains FOV and does not invent historical setup completion"))return false;
    if(!check(account_codec::encode(reopened.account,legacy,account_codec::kPriorVersion)
        && account_codec::decode(legacy,migrated,account_codec::kPriorVersion),"v1 profile wire compatibility"))return false;
    dawn::middleware::datagen::family4::account::preferences::Record preferences{};
    dawn::middleware::datagen::family4::account::preferences::BindingsRecord bindings{};
    if(!check(dawn::middleware::datagen::family4::account::preferences::encode(saved,preferences,bindings)
        && bindings.fieldOfViewAdjustment==105 && bindings.accountSeedVersion==1,
        "sign-in publishes saved FOV with closed seed gate"))return false;
    std::vector<std::byte> accountObject(dawn::middleware::datagen::family4::account::layout::kMinimumSize);
    auto publicationAccount=reopened.account;
    if(!check(state::prepare_account_runtime(publicationAccount)
        && publicationAccount.settings==saved,
        "runtime publication preparation preserves durable settings"))return false;
    if(!check(dawn::middleware::datagen::family4::account::encode(publicationAccount,accountObject),
        "sign-in account encoding with matched build data"))return false;
    if(!check(accountObject[0xB90]==std::byte{1},
        "sign-in publishes durable setup confirmation at native B90"))return false;
    auto explicitFalse=saved;
    if(!check(codec::parse_settings(message(settings_wire_fixture::setup_incomplete),saved,explicitFalse)
        && !explicitFalse.profileSetupCompleted && explicitFalse.controls==saved.controls,
        "explicit false has native boolean semantics"))return false;
    if(!check(account_codec::self_test(reopened.account),"version 7 storage round trip"))return false;
    std::puts("PASS: native full/partial inputs and FOV, malformed reports, durable reopen and v4/v5 migrations");
    return true;
}
}
