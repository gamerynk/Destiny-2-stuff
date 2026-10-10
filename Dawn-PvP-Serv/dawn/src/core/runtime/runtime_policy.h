#pragma once

#include <array>
#include <string_view>

namespace dawn::core::runtime::policy {

// without join/region/roster replies the unmodified client strands after joining
inline constexpr bool kActivityBackendReplication = true;

inline constexpr bool kDedicatedBackendRequired = true;

struct StateWriter {
    std::string_view id;
    bool enabled;
    std::string_view ownership;
    std::string_view cadence;
};

inline constexpr std::array kActivityStateWriters{
    StateWriter{"activity.session_store", true, "backend_session", "revisioned_events"},
    StateWriter{"activity.world_checkpoint", true, "backend_account", "transaction_commit"},
    StateWriter{"activity.join_result", true, "backend_session", "join_event"},
    StateWriter{"activity.entity_slots", true, "backend_session", "join_or_slot_event"},
    StateWriter{"activity.global_state", true, "backend_session", "join_and_liveness"},
    StateWriter{"activity.protocol_contract_state", true, "backend_connection", "committed_client_messages"},
    StateWriter{"activity.group_membership_profile", true, "backend_session", "join_or_profile_revision"},
    StateWriter{"activity.membership_region", kActivityBackendReplication, "backend_session", "snapshot_push"},
    StateWriter{"activity.roster_auth", kActivityBackendReplication, "backend_session", "snapshot_push"},
    StateWriter{"activity.destination_roster_bodies", kActivityBackendReplication, "backend_session", "roster_body"},
    StateWriter{"activity.keepalive_membership", kActivityBackendReplication, "backend_session", "periodic"},
    StateWriter{"activity.keepalive_roster_auth", kActivityBackendReplication, "backend_session", "periodic"},
};

struct ClientHook {
    std::string_view id;
    bool configuredToInstall;
    bool mutation;
    std::string_view role;
};

inline constexpr std::array kClientHooks{
    ClientHook{"boot.character_select_hold", true, true, "bootstrap"},
    ClientHook{"boot.orbit_slice_set", true, true, "bootstrap"},
    ClientHook{"boot.profile_setup_skip", true, true, "bootstrap"},
    ClientHook{"boot.solo_composition", true, true, "bootstrap"},
    ClientHook{"boot.tower_search_completion", true, true, "bootstrap"},
    ClientHook{"boot.orbit_handoff", true, true, "bootstrap"},
    ClientHook{"boot.dedicated_fireteam_pump", true, false, "native_descriptor_publication"},
    ClientHook{"queuez.service123_null_payload_guard", false, false, "retired_native_subscription_transport"},
};

void report() noexcept;

} // namespace dawn::core::runtime::policy
