#include "../../tests/fireteam_roster_self_test.h"
#include "../../tests/input_settings_self_test.h"
#include "../../tests/crucible_runtime_self_test.h"
#include "../../tests/account_name_self_test.h"
#include "../tests/weapon_picker_self_test.h"
#include <Windows.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <span>
#include <string_view>

#include "account_codec.h"
#include "account_http.h"
#include "account_store.h"
#include "activity_codec.h"
#include "expansions.h"
#include "core/filesystem/path.h"
#include "core/identity/device_identity.h"
#include "core/identity/local_ticket.h"
#include "core/logging/log.h"
#include "core/runtime/runtime_policy.h"
#include "core/settings/settings.h"
#include "client/hooks/egress/policy/policy.h"
#include "middleware/compression/oodle/runtime.h"
#include "middleware/bap/account_translation/account_translation_response.h"
#include "middleware/encoding/byte_order.h"
#include "middleware/datagen/definitions.h"
#include "middleware/datagen/family6/family6_encoder.h"
#include "middleware/web_service/messages/opcode503.h"
#include "middleware/web_service/web_service_envelope.h"
#include "middleware/runtime/middleware_runtime.h"
#include "server/bap/encrypted/queuez/queuez_state_validation.h"
#include "server/bap/encrypted/push/snapshot/snapshot.h"
#include "server/bap/runtime.h"
#include "server/http/server_http.h"
#include "server/runtime/server_runtime.h"
#include "server/web_service/web_service_runtime.h"
#include "state/build_data/runtime.h"
#include "state/build_data/abilities/ability_bucket_catalog.h"
#include "state/build_data/runtime/persistence/build_data_persistence.h"
#include "state/activity/entity_slots/runtime.h"
#include "state/activity/bubble_authority/runtime.h"
#include "state/activity/runtime.h"
#include "state/content_manifest/content_manifest_state_runtime.h"
#include "state/entitlements/entitlement_runtime.h"
#include "state/runtime/runtime.h"
#include "state/runtime/equipment/configured_equipment_identity.h"
#include "state/session/session_runtime.h"
#include "state/social/social_state.h"
#include "state/unlocks/unlocks_runtime.h"
#include "steam/runtime/runtime.h"
#include "../../tests/activity_account_context_self_test.h"
#include "../../tests/shared_activity_join_self_test.h"
#include "../../tests/activity_player_team_self_test.h"
#include "../../tests/activity_hello_auth_self_test.h"
#include "../../tests/matchmaking_context_capacity_self_test.h"
#include "../../tests/public_region_account_context_self_test.h"
#include "../../tests/private_startup_retirement_self_test.h"
#include "../../tests/concurrent_private_regions_self_test.h"
#include "../../tests/public_independent_arrival_self_test.h"
#include "../../tests/public_party_arrival_self_test.h"
#include "../../tests/social_primary_admission_self_test.h"
#include "../../tests/social_activity_route_self_test.h"
#include "../../tests/activity_join_policy_self_test.h"
#include "../../tests/dedicated_bap_capacity_self_test.h"
#include "../../tests/invisible_lobby_self_test.h"
#include "../../tests/invisible_lobby_handoff_self_test.h"
#include "../../tests/fireteam_transition_self_test.h"
#include "../../tests/late_private_join_self_test.h"
#include "../../tests/late_private_route_self_test.h"
#include "../../tests/social_coordinator_startup_self_test.h"
#include "../../tests/bap_owner_departure_self_test.h"

namespace {

std::atomic_bool g_running{true};

[[nodiscard]] bool public_http_surface_self_test() noexcept {
    using dawn::client::network::HttpMethod;
    using dawn::client::network::HttpRequest;
    using dawn::client::network::HttpResponse;
    std::array<std::byte, 256> output{};
    HttpResponse response{};
    const HttpRequest health{
        HttpMethod::get, "/healthz", {}, {}, {}, output};
    if (!dawn::server::http::consume_public(health, response)
        || response.statusCode != 200 || response.size == 0) {
        return false;
    }
    response = {};
    const HttpRequest owner{
        HttpMethod::get, "/owner", {}, {}, {}, output};
    if (dawn::server::http::consume_public(owner, response)) {
        return false;
    }
    response = {};
    const HttpRequest bot{
        HttpMethod::post, "/v1/bot/discord", {}, {}, {}, output};
    return !dawn::server::http::consume_public(bot, response);
}

[[nodiscard]] bool public_route_self_test() noexcept {
    namespace policy = dawn::client::hooks::egress::policy;
    const auto& external = dawn::core::settings::get().client.externalServer;
    for (std::size_t index = 0; index < external.portRouteCount; ++index) {
        const auto& route = external.portRoutes[index];
        sockaddr_in source{};
        source.sin_family = AF_INET;
        source.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        source.sin_port = htons(route.sourcePort);

        sockaddr_in redirected{};
        if (!policy::redirect_ipv4(reinterpret_cast<const sockaddr*>(&source),
                                   static_cast<int>(sizeof(source)),
                                   redirected)
            || redirected.sin_family != AF_INET
            || redirected.sin_port != htons(route.targetPort)
            || std::memcmp(&redirected.sin_addr,
                           route.address.data(),
                           route.address.size()) != 0
            || !policy::is_redirect_target(reinterpret_cast<const sockaddr*>(&redirected),
                                           static_cast<int>(sizeof(redirected)))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool account_translation_friend_first_self_test() noexcept {
    constexpr std::size_t kRequestSize = 12;
    constexpr std::size_t kIdentityOffset = 4;
    constexpr std::size_t kAccountSoidOffset = 12;
    constexpr std::size_t kPairedResponseSize = 20;
    constexpr std::size_t kEmptyResponseSize = 4;
    const std::uint64_t kFriendSteamId = dawn::steam::local_steam_id() + 2;
    constexpr std::uint64_t kOwnerSoid = 0x1122334455667788ULL;
    std::array<std::byte, kRequestSize> friendRequest{};
    std::array<std::byte, kRequestSize> ownerRequest{};
    std::array<std::byte, kPairedResponseSize> response{};
    std::span responseView{response};
    const auto prepare = [](std::span<std::byte, kRequestSize> request,
                            std::uint64_t identity) noexcept {
        dawn::middleware::encoding::write_u16_be(request.first<2>(), 1);
        request[2] = std::byte{0x0C};
        request[3] = std::byte{0xFF};
        dawn::middleware::encoding::write_u64_be(
            request.subspan<kIdentityOffset, 8>(), identity);
    };
    prepare(friendRequest, kFriendSteamId);
    prepare(ownerRequest, dawn::steam::local_steam_id());

    namespace translation = dawn::middleware::bap::account_translation;
    std::size_t written = 0;
    if (translation::request_matches_platform(friendRequest, dawn::steam::local_steam_id())
        || !translation::encode_response(friendRequest, 0, response, written)
        || written != kEmptyResponseSize
        || dawn::middleware::encoding::read_u16_be(responseView.first<2>()) != 0) {
        return false;
    }
    response.fill(std::byte{});
    if (!translation::request_matches_platform(ownerRequest, dawn::steam::local_steam_id())
        || !translation::encode_response(ownerRequest, kOwnerSoid, response, written)
        || written != kPairedResponseSize
        || dawn::middleware::encoding::read_u16_be(responseView.first<2>()) != 1
        || dawn::middleware::encoding::read_u64_be(
               responseView.subspan<kIdentityOffset, 8>()) != dawn::steam::local_steam_id()
        || dawn::middleware::encoding::read_u64_be(
               responseView.subspan<kAccountSoidOffset, 8>()) != kOwnerSoid) {
        return false;
    }
    response.fill(std::byte{});
    return !translation::request_matches_platform(friendRequest, dawn::steam::local_steam_id())
           && translation::encode_response(friendRequest, 0, response, written)
           && written == kEmptyResponseSize
           && dawn::middleware::encoding::read_u16_be(responseView.first<2>()) == 0;
}

[[nodiscard]] bool account_bootstrap_scope_self_test(std::uint64_t claimedSoid,
                                                     std::uint64_t accountSoid) noexcept {
    namespace encoding = dawn::middleware::encoding;
    namespace opcode503 = dawn::middleware::web_service::messages::opcode503;
    namespace web_service = dawn::middleware::web_service;

    constexpr std::size_t kRequestSize = web_service::kEnvelopeHeaderSize + 1 + sizeof(std::uint64_t);
    constexpr std::uint32_t kTransaction = 0x503A11U;
    std::array<std::byte, kRequestSize> request{};
    encoding::write_u16_be(std::span(request).first<2>(), opcode503::kOpcode);
    encoding::write_u32_be(std::span(request).subspan<2, 4>(), kTransaction);
    request[web_service::kEnvelopeHeaderSize] = std::byte{0xBF};
    encoding::write_u64_be(
        std::span(request).subspan<web_service::kEnvelopeHeaderSize + 1, sizeof(std::uint64_t)>(),
        claimedSoid);

    const dawn::state::AccountState before = dawn::state::account_snapshot();
    if (claimedSoid == 0 || accountSoid == 0 || before.primarySoid != accountSoid
        || !dawn::state::account::valid(before)) {
        return false;
    }

    web_service::Message message{};
    opcode503::Request canonical{};
    std::array<std::byte, 2048> expected{};
    std::size_t expectedSize = 0;
    if (!web_service::parse_request(request, message)
        || !opcode503::parse_request(message, canonical)) {
        return false;
    }
    canonical.primarySoid = accountSoid;
    canonical.hasPrimarySoid = true;
    const auto investment = dawn::state::investment_snapshot();
    if (!opcode503::encode_response(message, canonical, investment, expected, expectedSize)) {
        return false;
    }

    std::array<std::byte, 2048> actual{};
    std::size_t actualSize = 0;
    if (!dawn::server::web_service::consume(request, actual, actualSize)
        || actualSize != expectedSize
        || !std::equal(actual.begin(), actual.begin() + actualSize, expected.begin())) {
        return false;
    }
    const dawn::state::AccountState after = dawn::state::account_snapshot();
    return after.primarySoid == before.primarySoid
           && dawn::state::account::selected_character_soid(after)
                  == dawn::state::account::selected_character_soid(before)
           && dawn::state::account::valid(after);
}

[[nodiscard]] bool same_character_selection_self_test(
    const dawn::state::AccountState& account) noexcept {
    namespace datagen = dawn::middleware::datagen;
    namespace queuez = dawn::server::bap::encrypted::queuez;

    const std::uint64_t selected = dawn::state::account::selected_character_soid(account);
    std::uint32_t accountDefinition = 0;
    std::uint32_t characterDefinition = 0;
    if (account.primarySoid == 0 || selected == 0
        || !datagen::object_id(queuez::kAccountFamilyType,
                               datagen::kAccountSlot,
                               accountDefinition)
        || !datagen::object_id(queuez::kAccountFamilyType,
                               datagen::kCharacterSlot,
                               characterDefinition)) {
        return false;
    }

    queuez::SessionState before{};
    before.family4RootSoid = account.primarySoid;
    before.family3RootSoid = account.primarySoid;
    before.family4Residents[0] = queuez::ResidentObject{account.primarySoid, accountDefinition};
    before.family4Residents[1] = queuez::ResidentObject{selected, characterDefinition};
    before.family0Character = selected;
    before.family4Version = 7;
    before.family3Version = 3;
    before.family0Version = 2;
    before.family4ResidentCount = 2;
    before.family3Phase = queuez::Family3Phase::normal;
    before.family4Active = true;
    before.family3Active = true;
    before.family0Active = true;

    queuez::SelectCharacter staged{};
    return queuez::valid(before) && queuez::stage_select_character(before, selected, staged)
           && staged.previousCharacterSoid == selected
           && staged.selectedCharacterSoid == selected && !staged.patchAccount
           && staged.after.family4Version == before.family4Version + 1
           && staged.after.family4ResidentCount == before.family4ResidentCount
           && staged.after.family4Residents[1].objectSoid == selected
           && staged.after.family4Residents[1].definitionId == characterDefinition
           && staged.after.family3Phase == queuez::Family3Phase::normal
           && queuez::valid(staged.after);
}

struct Lifetime final {
    bool settings{};
    bool logging{};
    bool accountStore{};
    bool accountHttp{};
    bool expansions{};
    bool identityTicket{};
    HMODULE compression{};
    bool entitlements{};
    bool state{};
    bool accountResolver{};
    bool friendAccountResolver{};
    bool friendProfileResolver{};
    bool manifest{};
    bool middleware{};
    bool server{};
};

BOOL WINAPI control_handler(DWORD event) noexcept {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT
        || event == CTRL_SHUTDOWN_EVENT) {
        g_running.store(false, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

[[nodiscard]] bool initialize_manifest(HMODULE module) noexcept {
    HMODULE process = GetModuleHandleW(nullptr);
    dawn::core::path::Buffer packages;
    if (process == nullptr || !dawn::core::path::module_directory(process, packages)
        || !dawn::core::path::append(packages, L"packages")) {
        return false;
    }
    return dawn::state::content_manifest::initialize(
        module, std::wstring_view(packages.chars.data(), packages.length), true);
}

[[nodiscard]] HMODULE initialize_compression(HMODULE module) noexcept {
    dawn::core::path::Buffer library;
    if (!dawn::core::path::module_directory(module, library)
        || !dawn::core::path::append(library, L"bin\\x64\\oo2core_3_win64.dll")) {
        return nullptr;
    }
    HMODULE compression = LoadLibraryExW(library.chars.data(),
                                         nullptr,
                                         LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                                             | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (compression == nullptr) {
        return nullptr;
    }
    std::array<std::byte, 16> input{};
    std::array<std::byte, 1024> output{};
    std::size_t written = 0;
    if (!dawn::middleware::compression::oodle::compress(
            compression, input, output, written)
        || written == 0) {
        FreeLibrary(compression);
        return nullptr;
    }
    return compression;
}

[[nodiscard]] bool remote_details_anchor_test(HMODULE module) noexcept {
    using namespace dawn;
    namespace snapshots = server::bap::encrypted::push::snapshot;
    namespace datagen = middleware::datagen;
    const HMODULE compression = initialize_compression(module);
    if (compression == nullptr) return false;
    static server::bap::Scratch scratch{};
    static snapshots::Prepared prepared{};
    const auto reset = [&]() noexcept {
        prepared.family = {};
        prepared.objects = {};
        prepared.rawClearSize = 0;
        prepared.compressedClearSize = 0;
        SecureZeroMemory(&scratch, sizeof scratch);
    };
    state::social::RemoteAccount remote{};
    remote.accountSoid = state::account_snapshot().primarySoid ^ 0x100000000ULL;
    remote.platformId = 0x01100001000000F1ULL;
    remote.lobbyId = 0x01840000000000F1ULL;
    remote.relationship = state::social::Relationship::friendAccount;
    remote.personaState = 1;
    remote.joinable = false;
    middleware::queuez::Subscription subscription{};
    subscription.familyType = datagen::kFireteamFamily;
    subscription.familyRootSoid = remote.accountSoid;
    bool passed = true;
    const auto check = [&](bool value, const char* label) noexcept {
        if (!value) std::printf("FAIL: remote details anchor: %s\n", label);
        passed &= value;
    };
    const auto anchor = [&]() noexcept {
        reset();
        if (!snapshots::prepare_initial(scratch, subscription, prepared, &remote)
            || prepared.family.type != datagen::kFireteamFamily
            || prepared.family.rootSoid != remote.accountSoid
            || prepared.family.flags != middleware::queuez::kFullSnapshotFlag
            || prepared.family.objects.size() != 1
            || prepared.objects[0].id != datagen::kFireteamAnchorObjectId
            || prepared.objects[0].version != remote.accountSoid
            || prepared.rawClearSize != datagen::family6::kAnchorSize) return false;
        std::array<std::byte, datagen::family6::kAnchorSize> decoded{};
        if (!middleware::compression::oodle::decompress(
                compression, prepared.objects[0].payload, decoded)) return false;
        std::uint64_t root{}, link{};
        std::memcpy(&root, decoded.data(), sizeof root);
        std::memcpy(&link, decoded.data() + sizeof root, sizeof link);
        return root == remote.accountSoid && link == remote.accountSoid;
    };
    check(anchor(), "nonjoinable friend retains exact activity anchor, no fabricated party");
    remote.lobbyId = 0;
    check(anchor(), "friend without lobby retains activity anchor");
    reset();
    subscription.familyType = datagen::kJoinFamily;
    check(snapshots::prepare_initial(scratch, subscription, prepared, &remote)
              && prepared.family.objects.empty(), "Family7 remains unjoinable");
    subscription.familyType = datagen::kFireteamFamily;
    for (int relationship = 0; relationship <= 6; ++relationship) {
        if (relationship == 3) continue;
        remote.relationship = static_cast<state::social::Relationship>(relationship);
        reset();
        check(snapshots::prepare_initial(scratch, subscription, prepared, &remote)
                  && prepared.family.objects.empty(), "non-friend remains denied");
    }
    remote.relationship = state::social::Relationship::friendAccount;
    remote.platformId = 0;
    reset();
    check(snapshots::prepare_initial(scratch, subscription, prepared, &remote)
              && prepared.family.objects.empty(), "missing platform identity denied");
    remote.platformId = 0x01100001000000F1ULL;
    ++subscription.familyRootSoid;
    reset();
    check(snapshots::prepare_initial(scratch, subscription, prepared, &remote)
              && prepared.family.objects.empty(), "mismatched root denied");
    subscription.familyRootSoid = remote.accountSoid;
    reset();
    check(snapshots::prepare_initial(scratch, subscription, prepared)
              && prepared.family.objects.empty(), "unknown projection denied");
    reset();
    FreeLibrary(compression);
    if (passed) std::puts("PASS: remote details anchor: authenticated activity link independent of joinability; permission guards retained");
    return passed;
}

[[nodiscard]] bool self_details_anchor_test(HMODULE module) noexcept {
    using namespace dawn;
    namespace snapshots = server::bap::encrypted::push::snapshot;
    const auto owner = state::account_snapshot();
    const HMODULE compression = initialize_compression(module);
    if (owner.primarySoid == 0 || compression == nullptr) return false;
    static server::bap::Scratch scratch{};
    static snapshots::Prepared prepared{};
    const auto reset = [&]() noexcept {
        prepared.family = {};
        prepared.objects = {};
        prepared.rawClearSize = 0;
        prepared.compressedClearSize = 0;
        SecureZeroMemory(&scratch, sizeof scratch);
    };
    middleware::queuez::Subscription subscription{};
    subscription.familyType = middleware::datagen::kFireteamFamily;
    subscription.familyRootSoid = owner.primarySoid;
    reset();
    bool success = snapshots::prepare_initial(
                       scratch, subscription, prepared, nullptr, owner.primarySoid)
                   && prepared.family.type == middleware::datagen::kFireteamFamily
                   && prepared.family.rootSoid == owner.primarySoid
                   && prepared.family.version == 0
                   && prepared.family.flags == middleware::queuez::kFullSnapshotFlag
                   && prepared.family.objects.size() == 1
                   && prepared.objects[0].id == middleware::datagen::kFireteamAnchorObjectId
                   && prepared.objects[0].version == owner.primarySoid
                   && prepared.objects[0].encoding == middleware::queuez::Encoding::oodle
                   && prepared.rawClearSize == middleware::datagen::family6::kAnchorSize;
    std::array<std::byte, 16> decoded{};
    if (success) {
        success = middleware::compression::oodle::decompress(
            compression, prepared.objects[0].payload, decoded);
        std::uint64_t root{}, link{};
        std::memcpy(&root, decoded.data(), sizeof root);
        std::memcpy(&link, decoded.data() + sizeof root, sizeof link);
        success = success && root == owner.primarySoid && link == owner.primarySoid;
    }
    reset();
    success = success && snapshots::prepare_initial(scratch, subscription, prepared)
              && prepared.family.objects.empty();
    reset();
    const std::uint64_t otherRoot = owner.primarySoid ^ 0x100000000ULL;
    success = success && snapshots::prepare_initial(
                  scratch, subscription, prepared, nullptr, otherRoot)
              && prepared.family.objects.empty();
    static state::AccountState other{};
    static state::activity::ActivityState otherActivity{};
    other = owner;
    other.primarySoid = otherRoot;
    // ScopedAccountRuntime borrows this context; it must outlive the scope
    const state::AccountRuntimeContext otherRuntime{&other, &otherActivity};
    {
        const state::ScopedAccountRuntime scope(otherRuntime);
        reset();
        success = success && snapshots::prepare_initial(
                      scratch, subscription, prepared, nullptr, owner.primarySoid)
                  && prepared.family.objects.empty();
        subscription.familyRootSoid = otherRoot;
        reset();
        success = success && snapshots::prepare_initial(
                      scratch, subscription, prepared, nullptr, otherRoot)
                  && prepared.family.objects.size() == 1
                  && prepared.family.rootSoid == otherRoot;
        reset();
        success = success && snapshots::prepare_initial(scratch, subscription, prepared)
                  && prepared.family.objects.empty();
    }
    reset();
    SecureZeroMemory(&other, sizeof other);
    SecureZeroMemory(&otherActivity, sizeof otherActivity);
    FreeLibrary(compression);
    return success;
}

void shutdown(Lifetime& lifetime) noexcept {
    if (lifetime.server) {
        dawn::server::shutdown();
        lifetime.server = false;
    }
    if (lifetime.accountHttp) {
        dawn::server::http::unregister_extension_consumer(
            &restoration::account_http::consume);
        lifetime.accountHttp = false;
    }
    if (lifetime.expansions) {
        restoration::expansions::shutdown();
        lifetime.expansions = false;
    }
    if (lifetime.friendProfileResolver) {
        dawn::state::unregister_friend_profile_resolver(
            nullptr, &restoration::account_store::resolve_friend_profile_callback);
        lifetime.friendProfileResolver = false;
    }
    if (lifetime.friendAccountResolver) {
        dawn::state::unregister_friend_account_resolver(
            nullptr, &restoration::account_store::resolve_friend_account_callback);
        lifetime.friendAccountResolver = false;
    }
    if (lifetime.accountResolver) {
        dawn::state::unregister_account_runtime_resolver(
            nullptr, &restoration::account_store::resolve_runtime_callback);
        lifetime.accountResolver = false;
    }
    if (lifetime.middleware) {
        dawn::middleware::shutdown();
        lifetime.middleware = false;
    }
    if (lifetime.manifest) {
        dawn::state::content_manifest::shutdown();
        lifetime.manifest = false;
    }
    if (lifetime.state) {
        dawn::state::shutdown();
        lifetime.state = false;
    }
    dawn::state::social::clear();
    if (lifetime.accountStore) {
        restoration::account_store::shutdown();
        lifetime.accountStore = false;
    }
    if (lifetime.identityTicket) {
        dawn::core::identity::local_ticket::shutdown();
        lifetime.identityTicket = false;
    }
    if (lifetime.entitlements) {
        dawn::state::entitlements::clear();
        lifetime.entitlements = false;
    }
    dawn::state::unlocks::clear();
    if (lifetime.compression != nullptr) {
        FreeLibrary(lifetime.compression);
        lifetime.compression = nullptr;
    }
    if (lifetime.logging) {
        dawn::core::log::write(dawn::core::log::Channel::core,
                                  dawn::core::log::Level::info,
                                  "ev=server_host stage=shutdown result=ok");
        dawn::core::log::shutdown();
        lifetime.logging = false;
    }
    if (lifetime.settings) {
        dawn::core::settings::shutdown();
        lifetime.settings = false;
    }
}

[[nodiscard]] bool initialize(HMODULE module, Lifetime& lifetime, const char*& failed) noexcept {
    using namespace dawn;
    if (!core::settings::initialize(module)) {
        failed = "settings";
        return false;
    }
    lifetime.settings = true;
    state::unlocks::publish(core::settings::get().initialUnlocks);
    if (!core::log::initialize(
            module, core::settings::get().logging, core::log::ProcessRole::server)) {
        failed = "logging";
        return false;
    }
    lifetime.logging = true;
    core::runtime::policy::report();
    if (!core::identity::local_ticket::initialize(module)) {
        failed = "identity_ticket";
        return false;
    }
    lifetime.identityTicket = true;
    restoration::account_store::LoadResult accountLoad{};
    if (!restoration::account_store::initialize(
            module,
            core::settings::get().initialAccount,
            std::string_view(core::settings::get().steam.user.personaName.data()),
            steam::local_steam_id(),
            accountLoad)) {
        failed = "account_store";
        return false;
    }
    lifetime.accountStore = true;
    restoration::account_store::install_state_hooks();
    if (!state::social::publish(accountLoad.social)) {
        failed = "social_state";
        return false;
    }
    if (!server::http::register_extension_consumer(&restoration::account_http::consume)) {
        failed = "account_http";
        return false;
    }
    lifetime.accountHttp = true;
    lifetime.compression = initialize_compression(module);
    if (lifetime.compression == nullptr) {
        failed = "compression";
        return false;
    }
    core::log::write(core::log::Channel::server,
                     core::log::Level::info,
                     "ev=compression stage=oodle result=ok source=bin/x64");
    if (!state::entitlements::publish(core::settings::get().server.entitlements)) {
        failed = "entitlements";
        return false;
    }
    lifetime.entitlements = true;
    if (!restoration::expansions::initialize(module)) {
        failed = "expansions";
        return false;
    }
    lifetime.expansions = true;
    state::AccountRuntimeOptions accountOptions{};
    accountOptions.persistence =
        state::AccountPersistence{nullptr, &restoration::account_store::save_callback};
    accountOptions.preserveRuntimeFields = accountLoad.revision > 1;
    // durable gear is mutable, but the shared cache belongs to the authored equipment baseline;
    // an authored hash wins because this host can't extract build data
    accountOptions.buildDataEquipmentHash =
        core::settings::get().server.hasBuildDataEquipmentHash
            ? core::settings::get().server.buildDataEquipmentHash
            : state::runtime::equipment::configured_hash(core::settings::get().initialAccount);
    accountOptions.hasBuildDataEquipmentHash = true;
    if (core::settings::get().server.hasBuildDataEquipmentHash) {
        core::log::writef(core::log::Channel::server,
                          core::log::Level::info,
                          "ev=build_data stage=equipment_hash source=settings value=0x%016llX",
                          static_cast<unsigned long long>(accountOptions.buildDataEquipmentHash));
    }
    accountOptions.activityPersistence =
        state::ActivityPersistence{nullptr, &restoration::account_store::save_activity_callback};
    accountOptions.restoredActivity = accountLoad.activity;
    accountOptions.restoreActivity = accountLoad.activityRestored;
    if (!state::initialize(module,
                           accountLoad.account,
                           core::settings::get().initialActivityDefaults,
                           accountOptions)) {
        failed = "state";
        return false;
    }
    lifetime.state = true;
    if (!state::register_account_runtime_resolver(
            nullptr, &restoration::account_store::resolve_runtime_callback)) {
        failed = "account_resolver";
        return false;
    }
    lifetime.accountResolver = true;
    if (!state::register_friend_account_resolver(
            nullptr, &restoration::account_store::resolve_friend_account_callback)) {
        failed = "friend_account_resolver";
        return false;
    }
    lifetime.friendAccountResolver = true;
    if (!state::register_friend_profile_resolver(
            nullptr, &restoration::account_store::resolve_friend_profile_callback)) {
        failed = "friend_profile_resolver";
        return false;
    }
    lifetime.friendProfileResolver = true;
    if (accountLoad.revision == 1
        && !restoration::account_store::save(state::account_snapshot())) {
        failed = "account_seed_commit";
        return false;
    }
    if (!state::build_data::persist()) {
        failed = "build_data";
        return false;
    }
    if (!initialize_manifest(module)) {
        failed = "content_manifest";
        return false;
    }
    lifetime.manifest = true;
    if (!middleware::initialize()) {
        failed = "middleware";
        return false;
    }
    lifetime.middleware = true;
    if (!server::initialize_dedicated()) {
        failed = "transport";
        return false;
    }
    lifetime.server = true;
    core::log::write(core::log::Channel::core,
                     core::log::Level::info,
                     "ev=server_host stage=ready result=ok");
    return true;
}

[[nodiscard]] bool backend_self_test(HMODULE module) noexcept {
    using namespace dawn;
    if (module == nullptr || !core::settings::initialize(module)) {
        return false;
    }
    bool success = core::log::initialize(
        module, core::settings::get().logging, core::log::ProcessRole::server);
    const auto finish = [&]() noexcept {
        state::session::shutdown();
        state::social::clear();
        core::identity::device_identity::shutdown();
        core::identity::local_ticket::shutdown();
        restoration::account_store::shutdown();
        core::log::shutdown();
        core::settings::shutdown();
    };
    if (!success) {
        core::settings::shutdown();
        return false;
    }
    success = public_http_surface_self_test() && public_route_self_test()
              && account_translation_friend_first_self_test();
    const auto nowValue = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    const std::uint64_t now = nowValue > 0 ? static_cast<std::uint64_t>(nowValue) : 0;

    std::array<std::byte, core::identity::local_ticket::kTicketSize> ticket{};
    std::array<std::byte, core::identity::local_ticket::kTicketSize + 13> signOnBody{};
    core::identity::local_ticket::Claims claims{};
    success = now != 0 && core::identity::local_ticket::initialize(module)
              && core::identity::local_ticket::issue(steam::local_steam_id(), now, ticket);
    if (success) {
        std::copy(ticket.begin(), ticket.end(), signOnBody.begin() + 7);
        success = core::identity::local_ticket::authenticate(signOnBody, now, claims)
                  && claims.provider == core::identity::local_ticket::kProviderSteam
                  && claims.platformId == steam::local_steam_id();
    }
    if (success) {
        auto tampered = signOnBody;
        tampered[32] ^= std::byte{1};
        core::identity::local_ticket::Claims rejected{};
        success = !core::identity::local_ticket::authenticate(tampered, now, rejected);
    }

    std::array<std::byte, core::identity::device_identity::kProofSize> deviceProof{};
    core::identity::device_identity::Claims deviceClaims{};
    std::uint64_t devicePlatformId = 0;
    if (success) {
        success = core::identity::device_identity::initialize(module);
        devicePlatformId = core::identity::device_identity::platform_id();
        success = success && devicePlatformId != 0
                  && devicePlatformId != steam::local_steam_id()
                  && core::identity::device_identity::issue(
                      core::identity::device_identity::Scope::signOn,
                      now,
                      deviceProof)
                  && core::identity::device_identity::verify(
                      core::identity::device_identity::Scope::signOn,
                      deviceProof,
                      now,
                      deviceClaims)
                  && deviceClaims.platformId == devicePlatformId;
        success = success && deviceClaims.deviceId == devicePlatformId;
    }
    if (success) {
        core::identity::device_identity::Claims rejected{};
        auto tampered = deviceProof;
        tampered[40] ^= std::byte{1};
        success = !core::identity::device_identity::verify(
                      core::identity::device_identity::Scope::discordLinkCode,
                      deviceProof,
                      now,
                      rejected)
                  && !core::identity::device_identity::verify(
                      core::identity::device_identity::Scope::signOn,
                      tampered,
                      now,
                      rejected);
    }

    restoration::account_store::LoadResult first{};
    const auto& configured = core::settings::get();
    state::AccountState changedAccount{};
    if (success) {
        success = restoration::account_codec::self_test(configured.initialAccount)
                  && restoration::account_store::initialize(
                      module,
                      configured.initialAccount,
                      std::string_view(configured.steam.user.personaName.data()),
                      steam::local_steam_id(),
                      first);
    }
    if (success) {
        changedAccount = first.account;
        std::size_t selected = changedAccount.characterCount;
        for (std::size_t index = 0; index < changedAccount.characterCount; ++index) {
            if (changedAccount.characters[index].selected) {
                selected = index;
                break;
            }
        }
        const std::size_t next = selected < changedAccount.characterCount
                                     ? (selected + 1) % changedAccount.characterCount
                                     : 0;
        for (std::size_t index = 0; index < changedAccount.characterCount; ++index) {
            changedAccount.characters[index].selected = index == next;
        }
        success = changedAccount.characterCount != 0 && state::account::valid(changedAccount)
                  && restoration::account_store::save(changedAccount);
    }
    const std::uint64_t expectedRevision = first.revision + 1;
    restoration::account_store::shutdown();
    restoration::account_store::LoadResult second{};
    if (success) {
        success = restoration::account_store::initialize(
                      module,
                      configured.initialAccount,
                      std::string_view(configured.steam.user.personaName.data()),
                      steam::local_steam_id(),
                      second)
                  && second.revision == expectedRevision
                  && state::account::selected_character_soid(second.account)
                         == state::account::selected_character_soid(changedAccount)
                  && restoration::account_codec::self_test(second.account)
                  && restoration::account_store::save(second.account);
    }
    restoration::account_store::shutdown();
    restoration::account_store::LoadResult third{};
    if (success) {
        success = restoration::account_store::initialize(
                      module,
                      configured.initialAccount,
                      std::string_view(configured.steam.user.personaName.data()),
                      steam::local_steam_id(),
                      third)
                  && third.revision == expectedRevision
                  && state::account::selected_character_soid(third.account)
                         == state::account::selected_character_soid(changedAccount)
                  && restoration::account_codec::self_test(third.account);
    }
    if (success) {
        success = same_character_selection_self_test(third.account);
    }

    state::SignOnState bootstrap{};
    bootstrap.bootstrapTokenPresent = true;
    bootstrap.bootstrapToken.fill(std::byte{0xA5});
    state::session::Credentials issued{};
    state::session::Credentials authenticated{};
    if (success) {
        success = state::session::issue(third.account.primarySoid,
                                        steam::local_steam_id(),
                                        bootstrap,
                                        now,
                                        issued)
                  && state::session::authenticate(issued.signOn.sessionToken, now, authenticated)
                  && authenticated.accountSoid == third.account.primarySoid
                  && state::session::active(authenticated, now);
    }
    state::activity::ActivityState world = third.activity;
    if (success && !third.activityRestored) {
        world = {};
        world.defaults = configured.initialActivityDefaults;
        world.stateRevision = 2;
        world.nextSessionId = 2;
        world.allocatorRevision = 2;
        auto& record = world.sessions.front();
        record.destination = configured.initialActivityDefaults.defaultDestination.selection;
        record.sessionId = (third.account.primarySoid & 0xFFFFFFFF00000000ULL)
                           | 0x00200001ULL;
        record.ownerAccountSoid = third.account.primarySoid;
        record.createdRevision = world.stateRevision;
        record.recordRevision = world.stateRevision;
        record.occupied = true;
    }
    if (success) {
        success = restoration::activity_codec::self_test(world, third.account.primarySoid)
                  && restoration::account_store::save_activity(world);
    }
    restoration::account_store::shutdown();
    restoration::account_store::LoadResult fourth{};
    if (success) {
        success = restoration::account_store::initialize(
                      module,
                      configured.initialAccount,
                      std::string_view(configured.steam.user.personaName.data()),
                      steam::local_steam_id(),
                      fourth)
                  && fourth.activityRestored
                  && restoration::activity_codec::self_test(
                      fourth.activity, fourth.account.primarySoid);
    }
    std::uint64_t committedActivitySession = state::activity::kAbsentSessionId;
    bool stateInitialized = false;
    if (success) {
        state::AccountRuntimeOptions options{};
        options.persistence =
            state::AccountPersistence{nullptr, &restoration::account_store::save_callback};
        options.preserveRuntimeFields = true;
        options.buildDataEquipmentHash =
            configured.server.hasBuildDataEquipmentHash
                ? configured.server.buildDataEquipmentHash
                : state::runtime::equipment::configured_hash(configured.initialAccount);
        options.hasBuildDataEquipmentHash = true;
        options.activityPersistence = state::ActivityPersistence{
            nullptr, &restoration::account_store::save_activity_callback};
        options.restoredActivity = fourth.activity;
        options.restoreActivity = fourth.activityRestored;
        stateInitialized = state::initialize(
            nullptr, fourth.account, configured.initialActivityDefaults, options);
        success = stateInitialized;
    }
    if (success) {
        success = account_bootstrap_scope_self_test(fourth.account.primarySoid,
                                                    fourth.account.primarySoid);
    }
    if (success) {
        success = restoration::tests::activity_hello_auth_self_test(issued);
    }
    if (success) {
        success = restoration::tests::matchmaking_context_capacity_self_test(issued);
    }
    if (success) {
        success = restoration::tests::bap_owner_departure_self_test(issued);
    }
    if (success) {
        // retail wraps the app-ticket bytes in its own request fields. Accepting only an exact-size
        // device proof made Discord linking work while the game got HTTP 401 forever
        std::array<std::byte, core::identity::device_identity::kProofSize + 13>
            wrappedDeviceProof{};
        std::copy(deviceProof.begin(), deviceProof.end(), wrappedDeviceProof.begin() + 7);
        std::array<std::byte, 4096> signOnResponseBody{};
        client::network::HttpResponse deviceResponse{};
        const client::network::HttpRequest deviceRequest{
            client::network::HttpMethod::post,
            "/SignOn?lc=en",
            "application/octet-stream",
            {},
            wrappedDeviceProof,
            signOnResponseBody,
        };
        std::array<std::byte, state::kBootstrapTokenSize> bootstrapToken{};
        bootstrapToken.fill(std::byte{0xA5});
        success = state::publish_bootstrap_token(bootstrapToken)
                  && restoration::account_http::consume(deviceRequest, deviceResponse)
                  && deviceResponse.statusCode == 200 && deviceResponse.size != 0;

        std::array<std::byte, core::identity::device_identity::kProofSize + 13>
            wrappedOwnerTicket{};
        std::copy(ticket.begin(), ticket.end(), wrappedOwnerTicket.begin() + 7);
        client::network::HttpResponse ownerResponse{};
        const client::network::HttpRequest ownerRequest{
            client::network::HttpMethod::post,
            "/SignOn?lc=en",
            "application/octet-stream",
            {},
            wrappedOwnerTicket,
            signOnResponseBody,
        };
        success = success && !restoration::account_http::consume(ownerRequest, ownerResponse)
                  && server::http::consume(ownerRequest, ownerResponse)
                  && ownerResponse.statusCode == 200 && ownerResponse.size != 0;
        SecureZeroMemory(wrappedDeviceProof.data(), wrappedDeviceProof.size());
        SecureZeroMemory(wrappedOwnerTicket.data(), wrappedOwnerTicket.size());
        SecureZeroMemory(signOnResponseBody.data(), signOnResponseBody.size());
        SecureZeroMemory(bootstrapToken.data(), bootstrapToken.size());
    }
    if (success) {
        state::activity::PendingAllocation allocation{};
        state::activity::destination::DestinationSelection omega{};
        omega.activityIndex = 14;
        constexpr std::string_view package = "mission_scot";
        omega.packageNameLength = static_cast<std::uint8_t>(package.size());
        for (std::size_t index = 0; index < package.size(); ++index) {
            omega.packageName[index] = static_cast<std::int8_t>(package[index]);
        }
        omega.arrivalBubbleHash = 0xA83A9175U;
        omega.hasArrivalBubbleHash = true;
        omega.spawnSetHash = 0x4AB3287AU;
        omega.hasSpawnSetHash = true;
        success = state::activity::prepare_session(
                      omega, committedActivitySession, allocation)
                  && state::activity::commit(allocation);
    }
    if (success) {
        std::array<std::byte, core::identity::device_identity::kPublicKeySize> secondKey =
            deviceClaims.publicKey;
        secondKey.back() ^= std::byte{0x5A};
        std::uint64_t secondPlatform =
            0x0110000100000000ULL
            | (static_cast<std::uint32_t>(devicePlatformId) ^ 0x00800000U);
        if (secondPlatform == devicePlatformId || secondPlatform == steam::local_steam_id()) {
            secondPlatform ^= 0x00400000ULL;
        }
        restoration::account_store::DeviceBinding firstDevice{};
        restoration::account_store::DeviceBinding secondDevice{};
        success = restoration::account_store::load_or_create_device(
                      devicePlatformId,
                      devicePlatformId,
                      deviceClaims.publicKey,
                      firstDevice)
                  && restoration::account_store::load_or_create_device(
                      secondPlatform, secondPlatform, secondKey, secondDevice)
                  && firstDevice.accountSoid != secondDevice.accountSoid
                  && firstDevice.accountSoid != fourth.account.primarySoid
                  && secondDevice.accountSoid != fourth.account.primarySoid
                  && firstDevice.runtime.account != secondDevice.runtime.account
                  && firstDevice.runtime.activity != secondDevice.runtime.activity;
        const std::uint64_t ownerSelection = state::account::selected_character_soid(
            state::account_snapshot());
        const std::uint64_t secondSelection = success
                                                  ? state::account::selected_character_soid(
                                                        *secondDevice.runtime.account)
                                                  : 0;
        if (success) {
            const state::ScopedAccountRuntime firstScope(firstDevice.runtime);
            const state::AccountState before = state::account_snapshot();
            if (before.characterCount < 2) {
                success = false;
            } else {
                const std::uint64_t selected =
                    state::account::selected_character_soid(before);
                std::uint64_t target = before.characters[0].soid;
                if (target == selected) {
                    target = before.characters[1].soid;
                }
                bool changed = false;
                success = state::set_selected_character(target, changed) && changed
                          && state::account::selected_character_soid(
                                 state::account_snapshot())
                                 == target;
            }
        }
        if (success) {
            const state::ScopedAccountRuntime secondScope(secondDevice.runtime);
            constexpr std::uint64_t kSyntheticForeignBootstrapRoot =
                0xBEEF000100100100ULL;
            const std::uint64_t claimed =
                kSyntheticForeignBootstrapRoot != secondDevice.accountSoid
                    ? kSyntheticForeignBootstrapRoot
                    : (kSyntheticForeignBootstrapRoot ^ 0x0000000001000000ULL);
            success = account_bootstrap_scope_self_test(claimed, secondDevice.accountSoid)
                      && state::account::selected_character_soid(state::account_snapshot())
                             == secondSelection;
        }
        success = success
                  && state::account::selected_character_soid(state::account_snapshot())
                         == ownerSelection;
        if (success) {
            std::uint64_t migratedPlatform = devicePlatformId ^ 0x0000000000200000ULL;
            if (migratedPlatform == devicePlatformId
                || migratedPlatform == secondPlatform
                || migratedPlatform == steam::local_steam_id()) {
                migratedPlatform ^= 0x0000000000400000ULL;
            }
            restoration::account_store::DeviceBinding migrated{};
            success = restoration::account_store::load_or_create_device(
                          devicePlatformId,
                          migratedPlatform,
                          deviceClaims.publicKey,
                          migrated)
                      && migrated.accountSoid == firstDevice.accountSoid
                      && migrated.platformId == migratedPlatform
                      && migrated.runtime.account != nullptr
                      && migrated.runtime.account->primarySoid == firstDevice.accountSoid;
        }
        SecureZeroMemory(secondKey.data(), secondKey.size());
    }
    if (success) {
        state::activity::entity_slots::PendingMutation join{};
        success = state::activity::entity_slots::prepare_join(
                      committedActivitySession, 0x123456789ABCDEF0ULL, 64, 8, join)
                  && state::activity::entity_slots::commit(join)
                  && state::activity::contains(committedActivitySession)
                  && state::activity::is_joined(committedActivitySession);
    }
    if (success) {
        success = restoration::tests::activity_account_context_self_test(committedActivitySession);
        std::puts(success ? "PASS: production two-account activity join, leases, membership and isolation"
                          : "FAIL: production two-account activity lifecycle");
    }
    if (success) {
        success = restoration::tests::late_private_join_self_test(committedActivitySession)
            && restoration::tests::late_private_route_self_test(committedActivitySession)
            && restoration::tests::social_coordinator_startup_self_test()
            && restoration::tests::shared_activity_join_self_test(committedActivitySession)
            && restoration::tests::activity_player_team_self_test(committedActivitySession)
            && restoration::tests::public_region_account_context_self_test(committedActivitySession)
            && restoration::tests::public_independent_arrival_self_test(committedActivitySession)
            && restoration::tests::public_party_arrival_self_test(committedActivitySession)
            && restoration::tests::social_primary_admission_self_test(committedActivitySession)
            && restoration::tests::social_activity_route_self_test();
        std::puts(success ? "PASS: production public-region account context and background lifecycle"
                          : "FAIL: production public-region account context");
    }
    if (success) {
        success = server::bap::roster_presence_self_test(committedActivitySession);
        std::puts(success ? "PASS: production roster arrival commit/discard and owner/remote account isolation"
                          : "FAIL: production roster arrival/account isolation");
    }
    if (success) {
        success = self_details_anchor_test(module) && remote_details_anchor_test(module)
            && restoration::tests::invisible_lobby_self_test()
            && restoration::tests::invisible_lobby_handoff_self_test()
            && restoration::tests::fireteam_transition_self_test();
        std::puts(success ? "PASS: production self-details anchor, compression round-trip and account isolation"
                          : "FAIL: production self-details anchor/account isolation");
    }
    if (success) {
        success = restoration::tests::activity_join_policy_self_test()
            && restoration::tests::dedicated_bap_capacity_self_test();
    }
    if (success) {
        state::activity::bubble_authority::Grant grant{};
        success = state::activity::bubble_authority::select_grant(
                      committedActivitySession, 120, grant)
                  && grant.bubble == 15
                  && grant.token
                         == state::activity::bubble_authority::kInitialGrantToken;
        if (success) {
            state::activity::bubble_authority::record_grant(
                committedActivitySession, grant);
            state::activity::bubble_authority::Grant duplicate{};
            success = !state::activity::bubble_authority::select_grant(
                committedActivitySession, 120, duplicate);
        }
    }
    if (stateInitialized) {
        state::shutdown();
    }
    restoration::account_store::shutdown();
    restoration::account_store::LoadResult fifth{};
    if (success) {
        success = restoration::account_store::initialize(
                      module,
                      configured.initialAccount,
                      std::string_view(configured.steam.user.personaName.data()),
                      steam::local_steam_id(),
                      fifth)
                  && fifth.activityRestored
                  && fifth.activityRevision >= fourth.activityRevision + 2;
    }
    if (success) {
        bool restoredJoin = false;
        for (const auto& record : fifth.activity.sessions) {
            restoredJoin = restoredJoin
                           || (record.occupied
                               && record.sessionId == committedActivitySession
                               && record.ownerAccountSoid == fifth.account.primarySoid
                               && record.joined
                               && record.joinedRevision != state::activity::kInvalidRevision);
        }
        const std::uint64_t revisionBeforeNoOp = fifth.activityRevision;
        success = restoredJoin && restoration::account_store::save_activity(fifth.activity);
        restoration::account_store::shutdown();
        restoration::account_store::LoadResult sixth{};
        if (success) {
            success = restoration::account_store::initialize(
                          module,
                          configured.initialAccount,
                          std::string_view(configured.steam.user.personaName.data()),
                          steam::local_steam_id(),
                          sixth)
                      && sixth.activityRestored
                      && sixth.activityRevision == revisionBeforeNoOp;
        }
    }
    if (success) {
        auto wrongToken = issued.signOn.sessionToken;
        wrongToken[0] ^= std::byte{1};
        state::session::Credentials rejected{};
        success = !state::session::authenticate(wrongToken, now, rejected)
                  && !state::session::active(authenticated, issued.expiresAt);
    }
    state::session::Credentials replacement{};
    if (success) {
        const bool replacementIssued = state::session::issue(third.account.primarySoid,
                                                             steam::local_steam_id(),
                                                             bootstrap,
                                                             now + 1U,
                                                             replacement);
        const bool generationChanged = replacement.generation != issued.generation;
        const bool oldRevoked = !state::session::active(authenticated, now + 1U);
        const bool replacementActive = state::session::active(replacement, now + 1U);
        success = replacementIssued && generationChanged && oldRevoked && replacementActive;
        if (!success) {
            std::printf("Session generation self-test: issue=%u changed=%u revoked=%u active=%u\n",
                        static_cast<unsigned>(replacementIssued),
                        static_cast<unsigned>(generationChanged),
                        static_cast<unsigned>(oldRevoked),
                        static_cast<unsigned>(replacementActive));
        }
    }
    SecureZeroMemory(ticket.data(), ticket.size());
    SecureZeroMemory(signOnBody.data(), signOnBody.size());
    SecureZeroMemory(deviceProof.data(), deviceProof.size());
    SecureZeroMemory(&issued, sizeof issued);
    SecureZeroMemory(&authenticated, sizeof authenticated);
    SecureZeroMemory(&replacement, sizeof replacement);
    finish();
    return success;
}

} // namespace

int wmain(int argumentCount, wchar_t** arguments) {
    HMODULE module = GetModuleHandleW(nullptr);
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--weapon-picker-self-test") == 0) {
        if (!dawn::core::settings::initialize(module)) return 2;
        const auto& config = dawn::core::settings::get();
        const auto equipmentHash = config.server.hasBuildDataEquipmentHash
            ? config.server.buildDataEquipmentHash
            : dawn::state::runtime::equipment::configured_hash(config.initialAccount);
        const bool loaded = dawn::state::build_data::initialize(module, equipmentHash);
        const HMODULE compression = initialize_compression(module);
        const bool passed = loaded && compression && restoration::tests::weapon_picker_self_test(
            module, config.initialAccount, compression);
        if (compression) FreeLibrary(compression);
        restoration::account_store::shutdown();
        dawn::state::build_data::shutdown();
        dawn::core::settings::shutdown();
        std::printf("Weapon picker self-test: %s\n", passed ? "PASS" : "FAIL");
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--build-data-self-test") == 0) {
        if (!dawn::core::settings::initialize(module)) return 2;
        const auto& config = dawn::core::settings::get();
        const auto equipmentHash = config.server.hasBuildDataEquipmentHash
            ? config.server.buildDataEquipmentHash
            : dawn::state::runtime::equipment::configured_hash(config.initialAccount);
        const bool loaded = dawn::state::build_data::initialize(module, equipmentHash);
        const bool passed = loaded
            && dawn::state::build_data::runtime::persistence::all_domains_ready()
            && dawn::state::build_data::abilities::count() == 486;
        std::printf("Build data self-test: %s, ability selections=%zu\n",
                    passed ? "PASS" : "FAIL", dawn::state::build_data::abilities::count());
        dawn::state::build_data::shutdown();
        dawn::core::settings::shutdown();
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--private-startup-self-test") == 0) {
        if (!dawn::core::settings::initialize(module)) return 2;
        const auto& config = dawn::core::settings::get();
        const bool initialized = dawn::state::initialize(
            nullptr, config.initialAccount, config.initialActivityDefaults, {});
        bool passed = initialized && restoration::tests::concurrent_private_regions_self_test()
            && restoration::tests::private_startup_retirement_self_test();
        dawn::state::activity::PendingAllocation fixture{};
        std::uint64_t fixtureId{};
        passed = passed && dawn::state::activity::prepare_session(fixtureId,fixture)
            && dawn::state::activity::commit(fixture)
            && restoration::tests::public_region_account_context_self_test(fixtureId)
            && restoration::tests::activity_player_team_self_test(fixtureId);
        if (initialized) dawn::state::shutdown();
        dawn::core::settings::shutdown();
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--private-match-self-test") == 0) {
        if (!dawn::core::settings::initialize(module)) return 2;
        const auto& config = dawn::core::settings::get();
        const bool initialized = dawn::state::initialize(
            nullptr, config.initialAccount, config.initialActivityDefaults, {});
        const bool passed = initialized && restoration::tests::activity_player_team_self_test(0);
        if (initialized) dawn::state::shutdown();
        dawn::core::settings::shutdown();
        std::printf("Private match self-test: %s\n", passed ? "PASS" : "FAIL");
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--fireteam-transition-self-test") == 0) {
        return restoration::tests::fireteam_transition_self_test()
            && restoration::tests::invisible_lobby_self_test()
            && restoration::tests::invisible_lobby_handoff_self_test() ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--backend-self-test") == 0) {
        const bool passed = backend_self_test(module);
        std::printf("Restoration backend self-test: %s\n", passed ? "PASS" : "FAIL");
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--account-name-self-test") == 0) {
        if (!dawn::core::settings::initialize(module)) return 2;
        const bool passed = restoration::tests::account_name_self_test(
            module, dawn::core::settings::get().initialAccount);
        dawn::core::settings::shutdown();
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--fireteam-roster-self-test") == 0) {
        if (!dawn::core::settings::initialize(module)) return 2;
        const bool passed = restoration::tests::fireteam_roster_self_test(
            module, dawn::core::settings::get().initialAccount);
        dawn::core::settings::shutdown();
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--input-settings-self-test") == 0) {
        if (!dawn::core::settings::initialize(module)) return 2;
        // the real Family4 publication needs the matched catalog; a prior --build-data-self-test ran in another process
        const auto& config = dawn::core::settings::get();
        const auto equipmentHash = config.server.hasBuildDataEquipmentHash
            ? config.server.buildDataEquipmentHash
            : dawn::state::runtime::equipment::configured_hash(config.initialAccount);
        const bool loaded = dawn::state::build_data::initialize(module, equipmentHash);
        const bool passed = loaded && restoration::tests::input_settings_self_test(
            module, config.initialAccount);
        dawn::state::build_data::shutdown();
        dawn::core::settings::shutdown();
        return passed ? 0 : 2;
    }
    if (argumentCount == 2 && arguments != nullptr && arguments[1] != nullptr
        && std::wcscmp(arguments[1], L"--crucible-runtime-self-test") == 0) {
        return restoration::tests::crucible_runtime_self_test()?0:2;
    }
    Lifetime lifetime;
    const char* failed = "module";
    if (module == nullptr || !initialize(module, lifetime, failed)) {
        std::fprintf(stderr, "RestorationServer failed during %s initialization.\n", failed);
        shutdown(lifetime);
        return 1;
    }
    (void)SetConsoleCtrlHandler(&control_handler, TRUE);
    const auto& serverSettings = dawn::core::settings::get().server;
    std::printf("RestorationServer is listening on 127.0.0.1 "
                "(discovery UDP 3074/3075, HTTP %u, BAP %u, gameplay %u).\n",
                static_cast<unsigned>(serverSettings.httpPort),
                static_cast<unsigned>(serverSettings.bapPort),
                static_cast<unsigned>(serverSettings.gameplay.port));
    std::printf("Waiting for the client bootstrap. Press Ctrl+C to stop.\n");
    while (g_running.load(std::memory_order_acquire)) {
        dawn::server::service(GetTickCount64());
        Sleep(1);
    }
    shutdown(lifetime);
    return 0;
}
