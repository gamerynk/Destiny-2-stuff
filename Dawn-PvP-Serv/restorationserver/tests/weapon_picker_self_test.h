#pragma once
#include <cstdio>
#include <chrono>
#include <string>
#include "../src/account_http.h"
#include "core/identity/device_identity.h"
#include "server/http/server_http.h"
#include "server/bap/runtime.h"
#include "server/bap/encrypted/internal.h"
#include <vector>
#include "../src/account_store.h"
#include "state/build_data/runtime.h"
#include "state/runtime/state_account_transaction_helpers.h"
#include "state/account/inventory/weapon_roll_policy.h"
#include "state/build_data/items/details/item_detail_catalog.h"
#include "middleware/datagen/family4/loadout/loadout_resolver.h"
#include "middleware/datagen/family4/loadout/loadout_item_resolver.h"
namespace restoration::tests {
inline bool weapon_picker_self_test(void* module, const dawn::state::AccountState& seed,
                                   HMODULE compression) noexcept {
    const auto fail = [](int line) { std::printf("FAIL: weapon picker at source line %d\n", line); return false; };
    namespace store = account_store;
    namespace data = dawn::state::build_data;
    constexpr std::uint64_t platform = 0x01100001F0000006ULL;
    namespace id = dawn::core::identity::device_identity;
    namespace http = dawn::server::http;
    struct Cleanup { ~Cleanup() {
        http::unregister_extension_consumer(&account_http::consume);
        id::shutdown(); store::shutdown();
    }} cleanup;
    store::LoadResult loaded{};
    if (!store::initialize(module, seed, "Weapon test", platform + 10, loaded)
        || !id::initialize(module, platform)) return fail(__LINE__);
    const auto now = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::array<std::byte, id::kProofSize> proof{}, wrongScope{}, expired{};
    id::Claims claims{};
    store::DeviceBinding binding{};
    if (!id::issue(id::Scope::weaponAcquire, now, proof)
        || !id::verify(id::Scope::weaponAcquire, proof, now, claims)
        || !store::load_or_create_device(claims.deviceId, platform, claims.publicKey, binding)
        || !id::issue(id::Scope::accountRename, now, wrongScope)
        || !id::issue(id::Scope::weaponAcquire, now - 3600, expired)
        || !http::register_extension_consumer(&account_http::consume)) return fail(__LINE__);
    dawn::state::AccountRuntimeContext context = binding.runtime;
    if (!context.account || !context.account->characterCount) return fail(__LINE__);
    for (std::size_t i = 0; i < context.account->characterCount; ++i) context.account->characters[i].selected = i == 0;
    store::WeaponAcquireRequest request{};
    request.requestId = 0x197513123456789ULL;
    request.characterSoid = context.account->characters[0].soid;
    std::vector<data::items::details::Definition> details(data::items::details::count());
    std::size_t count = 0;
    if (!data::items::details::snapshot(details, count)) return fail(__LINE__);
    dawn::state::account::inventory::weapon_roll::Catalog policy;
    if (!policy.build()) return fail(__LINE__);
    bool found = false;
    for (const auto& detail : details) {
        if (!dawn::state::account::inventory::weapon_roll::is_weapon(detail) || !detail.ordinarySocketCount) continue;
        request.definitionHash = detail.definitionHash; request.plugCount = detail.ordinarySocketCount;
        request.plugs = {};
        bool valid = true;
        for (std::size_t lane = 0; lane < request.plugCount; ++lane) {
            if (detail.initialPlugIndices[lane] == data::items::details::kUnavailableItemIndex) continue;
            data::items::Definition plug{};
            valid = valid && data::find_item_definition_index(detail.initialPlugIndices[lane], plug);
            request.plugs[lane] = plug.definitionHash;
        }
        if (!valid) continue;
        for (std::size_t lane = 0; lane < request.plugCount && !found; ++lane) {
            for (auto plugIndex : policy.candidates(detail.definitionIndex, lane)) {
                if (data::is_socket_plug_allowed(detail.definitionIndex, static_cast<std::uint8_t>(lane), plugIndex)) continue;
                data::items::Definition plug{};
                if (!data::find_item_definition_index(plugIndex, plug)) continue;
                request.plugs[lane] = plug.definitionHash; found = true; break;
            }
        }
        if (found) break;
    }
    if (!found) return fail(__LINE__);
    std::uint32_t firstPlug = 0, secondPlug = 0;
    for (auto hash : request.plugs) {
        if (!hash) continue;
        if (!firstPlug) firstPlug = hash;
        else if (hash != firstPlug) { secondPlug = hash; break; }
    }
    if (!firstPlug || !secondPlug
        || !dawn::middleware::datagen::family4::loadout::weapon_picker_curated_roll_self_test(firstPlug, secondPlug))
        return fail(__LINE__);
    auto send = [&](std::span<const std::byte> auth, std::string_view payload) {
        std::vector<std::byte> body(auth.begin(), auth.end());
        for (char ch : payload) body.push_back(static_cast<std::byte>(ch));
        std::array<std::byte, 512> output{};
        dawn::client::network::HttpRequest req{dawn::client::network::HttpMethod::post,
            "/v1/account/weapon/acquire", "application/octet-stream", {}, body, output};
        dawn::client::network::HttpResponse response{};
        const auto status = http::consume_public(req, response) ? response.statusCode : 0U;
        std::printf("weapon HTTP %u: %.*s\n", status, static_cast<int>(response.size), reinterpret_cast<const char*>(output.data()));
        return status;
    };
    const auto payloadFor = [](const store::WeaponAcquireRequest& value) {
        std::string payload = std::to_string(value.requestId) + " " + std::to_string(value.characterSoid)
            + " " + std::to_string(value.definitionHash) + " " + std::to_string(value.plugCount);
        for (std::size_t lane = 0; lane < value.plugCount; ++lane) payload += " " + std::to_string(value.plugs[lane]);
        return payload;
    };
    auto forged = proof; forged.back() ^= std::byte{1};
    const auto payload = payloadFor(request);
    if (send({}, payload) != 401 || send(wrongScope, payload) != 401
        || send(forged, payload) != 401 || send(expired, payload) != 401
        || send(proof, "1 2 3") != 400 || send(proof, payload + " 1") != 400
        || send(proof, "-1 0 1 0") != 400 || send(proof, std::string(513, '1')) != 401
        || send(proof, "1 0 4294967296 0") != 400 || send(proof, "1 0 1 13") != 400) return fail(__LINE__);
    store::WeaponAcquireResult first{}, retry{}; std::string_view error{};
    const auto before = context.account->characters[0].inventory.count;
    if (send(proof, payload) != 200 || !store::acquire_weapon(platform, request, first, error)) {
        std::printf("weapon grant failed: %.*s\n", static_cast<int>(error.size()), error.data()); return fail(__LINE__);
    }
    if (context.account->characters[0].inventory.count != before + 1
        || !store::acquire_weapon(platform, request, retry, error)
        || retry.instanceSoid != first.instanceSoid || retry.revision != first.revision
        || context.account->characters[0].inventory.count != before + 1) return fail(__LINE__);
    std::uint64_t ordinaryInstance = 0;
    if (!dawn::state::runtime::detail::next_item_instance_soid(*context.account, ordinaryInstance)
        || dawn::state::account::inventory::is_weapon_picker_instance(ordinaryInstance)) return fail(__LINE__);
    auto conflict = request; conflict.plugs[0] ^= 1;
    if (store::acquire_weapon(platform, conflict, retry, error)) return fail(__LINE__);
    auto invalid = request; ++invalid.requestId; invalid.characterSoid ^= 1;
    if (store::acquire_weapon(platform, invalid, retry, error)) return fail(__LINE__);
    context.account->characters[0].selected = false;
    invalid = request; ++invalid.requestId;
    if (store::acquire_weapon(platform, invalid, retry, error)) return fail(__LINE__);
    context.account->characters[0].selected = true;
    invalid = request; ++invalid.requestId; invalid.definitionHash = 0;
    if (store::acquire_weapon(platform, invalid, retry, error)) return fail(__LINE__);
    bool nonweaponChecked = false;
    for (const auto& detail : details) {
        if (dawn::state::account::inventory::weapon_roll::is_weapon(detail)) continue;
        invalid = request; ++invalid.requestId; invalid.definitionHash = detail.definitionHash;
        invalid.plugCount = detail.ordinarySocketCount;
        if (store::acquire_weapon(platform, invalid, retry, error)) return fail(__LINE__);
        nonweaponChecked = true; break;
    }
    if (!nonweaponChecked) return fail(__LINE__);
    invalid = request; ++invalid.requestId; invalid.plugs[0] = 0xDEADBEEF;
    if (store::acquire_weapon(platform, invalid, retry, error)) return fail(__LINE__);
    data::items::Definition weapon{};
    if (!data::find_item_definition_hash(request.definitionHash, weapon)) return fail(__LINE__);
    bool wrongLaneChecked = false;
    for (const auto& detail : details) {
        for (std::size_t lane = 0; lane < detail.ordinarySocketCount && !wrongLaneChecked; ++lane) {
            for (const auto plugIndex : policy.candidates(detail.definitionIndex, lane)) {
                if (policy.allowed(weapon.definitionIndex, 0, plugIndex)) continue;
                data::items::Definition plug{};
                if (!data::find_item_definition_index(plugIndex, plug)) continue;
                invalid = request; ++invalid.requestId; invalid.plugs[0] = plug.definitionHash;
                if (store::acquire_weapon(platform, invalid, retry, error)) return fail(__LINE__);
                wrongLaneChecked = true; break;
            }
        }
        if (wrongLaneChecked) break;
    }
    if (!wrongLaneChecked) return fail(__LINE__);
    if (store::acquire_weapon(platform + 1, request, retry, error)) return fail(__LINE__);
    dawn::middleware::datagen::family4::loadout::ResolvedLoadout resolved{};
    if (!dawn::middleware::datagen::family4::loadout::resolve(*context.account, 0, resolved)) return fail(__LINE__);
    bool encoded = false;
    for (std::size_t i = 0; i < resolved.itemCount; ++i) {
        const auto& row = resolved.items[i];
        if (row.instance.instanceSoid != first.instanceSoid) continue;
        if (row.equipped || row.quantity != 1 || row.mutationSerial <= 0) return fail(__LINE__);
        for (std::size_t lane = 0; lane < request.plugCount; ++lane) {
            data::items::Definition plug{};
            if (request.plugs[lane]) {
                if (!data::find_item_definition_hash(request.plugs[lane], plug)
                    || row.instance.ordinarySockets.plugs[lane] != plug.definitionIndex) return fail(__LINE__);
            } else if (row.instance.ordinarySockets.plugs[lane]) return fail(__LINE__);
        }
        std::array<std::byte, dawn::middleware::datagen::family4::instance::layout::kObjectSize> bytes{};
        encoded = dawn::middleware::datagen::family4::instance::encode(row.instance, bytes);
    }
    if (!encoded) return fail(__LINE__);
    {
        const dawn::state::ScopedAccountRuntime scope(context);
        if (!dawn::server::bap::encrypted::weapon_acquisition_push_self_test(
                first.characterSoid, first.instanceSoid, compression)
            || !dawn::server::bap::inventory_feedback_publication_self_test()) return fail(__LINE__);
        std::puts("PASS: native acquisition ordering, compressed feedback, failed-send retry, batching and gameplay publication");
    }
    const auto retained = *context.account;
    auto& full = context.account->characters[0];
    const auto acquired = full.inventory.values[full.inventory.count - 1];
    for (unsigned i = 0; i < 11 && full.inventory.count < full.inventory.values.size(); ++i) {
        auto extra = acquired; extra.instanceSoid = 0x4DC0000000000000ULL + i;
        extra.mutationSerial = static_cast<std::int32_t>(full.nextInventorySerial++);
        full.inventory.values[full.inventory.count++] = extra;
    }
    invalid = request; ++invalid.requestId;
    const auto fullCount = full.inventory.count;
    if (store::acquire_weapon(platform, invalid, retry, error) || full.inventory.count != fullCount) return fail(__LINE__);
    *context.account = retained;
    store::shutdown();
    store::LoadResult reopened{};
    if (!store::initialize(module, seed, "Weapon test", platform + 10, reopened)) return fail(__LINE__);
    std::array<char, store::kAccountNameCapacity> reopenedName{};
    if (!store::profile_by_platform(platform, reopened.account, reopened.revision, reopenedName)) return fail(__LINE__);
    bool persisted = false;
    for (std::size_t i = 0; i < reopened.account.characters[0].inventory.count; ++i)
        persisted = persisted || reopened.account.characters[0].inventory.values[i].instanceSoid == first.instanceSoid;
    if (!persisted || !store::acquire_weapon(platform, request, retry, error)
        || retry.instanceSoid != first.instanceSoid) return fail(__LINE__);
    std::puts("weapon picker grant/persistence/identity/idempotency/resolution passed");
    return true;
}
}
