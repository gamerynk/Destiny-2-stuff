set(ROOT ${PROJECT_SOURCE_DIR})
set(SRC ${ROOT}/dawn/src)
set(SERVER ${ROOT}/restorationserver/src)
set(VENDOR ${ROOT}/dawn/vendor)
set(AM ${SRC}/middleware/bap/activity_message)
set(TESTS ${CMAKE_CURRENT_SOURCE_DIR})

# Some tests need files that aren't in the repo. Point these at your own copies to run them.
set(DAWN_BUILD_DATA "" CACHE FILEPATH "build_data.bin used by the catalog and cache tests")
set(DAWN_NATIVE_TEXT "" CACHE FILEPATH "locally generated native-text.bin")

function(dawn_flags target)
    cmake_parse_arguments(F "" "" "DEFINES;LIBS;INCLUDES" ${ARGN})
    target_compile_options(${target} PRIVATE /W4 /WX /EHsc)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX ${F_DEFINES})
    target_include_directories(${target} PRIVATE ${SRC} ${F_INCLUDES})
    if(F_LIBS)
        target_link_libraries(${target} PRIVATE ${F_LIBS})
    endif()
endfunction()

# dawn_test(name main.cpp [SOURCES ...] [DEFINES ...] [LIBS ...] [INCLUDES ...] [ARGS ...]
#           [FROM_ROOT] [NO_RUN])
# FROM_ROOT runs the test from the repo root, for tests that open files under tests/.
function(dawn_test name main)
    cmake_parse_arguments(T "FROM_ROOT;NO_RUN" "" "SOURCES;DEFINES;LIBS;INCLUDES;ARGS" ${ARGN})
    add_executable(${name} ${main} ${T_SOURCES})
    dawn_flags(${name} DEFINES ${T_DEFINES} LIBS ${T_LIBS} INCLUDES ${T_INCLUDES})
    if(T_NO_RUN)
        return()
    endif()
    set(dir ${CMAKE_CURRENT_BINARY_DIR})
    if(T_FROM_ROOT)
        set(dir ${ROOT})
    endif()
    add_test(NAME ${name} COMMAND ${name} ${T_ARGS} WORKING_DIRECTORY ${dir})
endfunction()

# Translation units that only have to compile under a given set of defines.
function(dawn_compile name)
    cmake_parse_arguments(C "" "" "SOURCES;DEFINES;INCLUDES" ${ARGN})
    add_library(${name} OBJECT ${C_SOURCES})
    dawn_flags(${name} DEFINES ${C_DEFINES} INCLUDES ${C_INCLUDES})
endfunction()

set(BIT_CODEC
    ${SRC}/middleware/encoding/bit_raw.cpp
    ${SRC}/middleware/encoding/bit_reader.cpp
    ${SRC}/middleware/encoding/bit_writer.cpp)
set(PROTOBUF
    ${SRC}/middleware/protobuf/protobuf_wire.cpp
    ${SRC}/middleware/protobuf/protobuf_reader.cpp
    ${SRC}/middleware/protobuf/protobuf_writer.cpp)
set(ACTIVITY_MESSAGE
    ${SRC}/middleware/encoding/bit_reader.cpp
    ${SRC}/middleware/encoding/bit_writer.cpp
    ${AM}/activity_authoritative_data_parser.cpp
    ${AM}/activity_authoritative_opaque_skip.cpp
    ${AM}/activity_join_request_parser.cpp
    ${AM}/activity_replicate_membership_encoder.cpp
    ${AM}/activity_membership_member_writer.cpp
    ${AM}/activity_membership_region_writer.cpp
    ${AM}/activity_sensor_auth_bodies.cpp
    ${AM}/activity_sensor_auth_blocks.cpp
    ${AM}/activity_sensor_auth_encoder.cpp)
set(GROUP_SESSION
    ${SRC}/middleware/gameplay/group/session_messages.cpp
    ${SRC}/middleware/gameplay/group/session_state.cpp
    ${BIT_CODEC} ${PROTOBUF}
    ${SRC}/middleware/crypto/lookup3.cpp)
set(SIGNON
    ${SRC}/middleware/signon/response.cpp
    ${SRC}/middleware/signon/config/signon_config_blob.cpp
    ${SRC}/middleware/signon/extended/signon_extended_fields.cpp
    ${SRC}/middleware/signon/ownership/signon_ownership_encoder.cpp)
set(QUEUEZ_STAGING
    ${SRC}/server/bap/encrypted/queuez/staging/queuez_family_staging.cpp
    ${SRC}/server/bap/encrypted/queuez/queuez_state_validation.cpp)
set(WEB_SERVICE
    ${SRC}/middleware/web_service/web_service_envelope.cpp
    ${SRC}/middleware/web_service/status_fields.cpp)
set(IMGUI_INCLUDES ${VENDOR}/imgui ${VENDOR}/imgui/backends ${VENDOR}/detours)
set(CURSOR ${SRC}/client/hooks/cursor)
set(GRAPHICS ${SRC}/client/hooks/graphics)

# --- account and session

dawn_compile(bap_plaintext SOURCES ${SRC}/server/bap/plaintext.cpp)
dawn_test(account_session_selection_test ${TESTS}/account_session_selection_test.cpp
    SOURCES ${SRC}/state/session/session_runtime.cpp
            ${SRC}/state/runtime/state_account_session_runtime.cpp
    LIBS bcrypt)
dawn_test(account_translation_test ${TESTS}/account_translation_test.cpp
    SOURCES ${SRC}/middleware/bap/account_translation/account_translation_response.cpp)
dawn_test(app_ticket_renewal_universal ${TESTS}/app_ticket_renewal_test.cpp
    DEFINES RESTORATION_UNIVERSAL_CLIENT=1)
dawn_test(app_ticket_renewal_owner ${TESTS}/app_ticket_renewal_test.cpp)
dawn_test(character_banner_summary_test ${TESTS}/character_banner_summary_test.cpp
    SOURCES ${SRC}/middleware/datagen/character_record/character_record_encoder.cpp
            ${SRC}/middleware/datagen/family1/family1_encoder.cpp)
dawn_test(profile_setup_persistence_test ${TESTS}/profile_setup_persistence_test.cpp
    SOURCES ${SERVER}/account_codec.cpp
            ${SRC}/middleware/web_service/messages/opcode701_codec.cpp
            ${SRC}/middleware/encoding/bit_reader.cpp
            ${SRC}/middleware/datagen/family4/account/account_encoder.cpp
            ${SRC}/middleware/datagen/family4/account/preferences/preferences_encoder.cpp
            ${SRC}/middleware/datagen/roster/queuez_roster.cpp
            ${SRC}/middleware/datagen/family4/progression/progression_bank_keys.cpp
            ${SRC}/state/account/account_state.cpp
            ${SRC}/state/account/inventory/inventory_state.cpp
            ${SRC}/state/account/settings/settings_state.cpp
            ${SRC}/state/account/settings/account_input_settings_runtime.cpp
            ${SRC}/state/unlocks/unlocks_runtime.cpp)
dawn_test(public_player_profile_contract_test ${TESTS}/public_platform_identity_contract_test.cpp
    SOURCES ${SRC}/middleware/gameplay/group/member_messages.cpp ${GROUP_SESSION}
            ${SRC}/middleware/bap/account_translation/account_translation_response.cpp)

# --- activity messages

dawn_test(activity_join_result_encoder_contract_test
    ${TESTS}/activity_join_result_encoder_contract_test.cpp
    SOURCES ${AM}/activity_join_result_encoder.cpp)
dawn_test(activity_membership_citizen_contract_test
    ${TESTS}/activity_membership_citizen_contract_test.cpp
    SOURCES ${AM}/activity_membership_member_writer.cpp
            ${AM}/activity_membership_region_writer.cpp
            ${AM}/activity_replicate_membership_encoder.cpp
            ${SRC}/middleware/encoding/bit_writer.cpp)
foreach(test activity_player_team_contract_test native_countdown_producer_test
        native_participation_roster_test)
    dawn_test(${test} ${TESTS}/${test}.cpp SOURCES ${ACTIVITY_MESSAGE} DEFINES RUN97_CANDIDATE)
endforeach()
dawn_test(activity_public_target_readiness_policy_test
    ${TESTS}/activity_public_target_readiness_policy_test.cpp)
dawn_test(activity_spawn_sync_contract_test ${TESTS}/activity_spawn_sync_contract_test.cpp
    SOURCES ${SRC}/server/bap/encrypted/push/activity/activity_spawn_sync.cpp)
dawn_test(native_clock_configuration_test ${TESTS}/native_clock_configuration_test.cpp
    SOURCES ${SRC}/middleware/encoding/bit_reader.cpp ${SRC}/middleware/encoding/bit_writer.cpp)
dawn_test(native_clock_publication_test ${TESTS}/native_clock_publication_test.cpp
    SOURCES ${SRC}/middleware/encoding/bit_writer.cpp
            ${SRC}/server/bap/encrypted/push/activity/activity_roster_push.cpp)

# --- boot flow and fireteam bootstrap

set(FIRETEAM_BOOTSTRAP ${SRC}/client/hooks/bootflow/dedicated_fireteam_bootstrap.cpp)
dawn_compile(fireteam_bootstrap_compat SOURCES ${FIRETEAM_BOOTSTRAP})
dawn_compile(fireteam_bootstrap_native SOURCES ${FIRETEAM_BOOTSTRAP}
    DEFINES RESTORATION_NATIVE_FIRETEAM_CALLBACK=1)
dawn_compile(fireteam_bootstrap_camera_compat SOURCES ${FIRETEAM_BOOTSTRAP}
    DEFINES RESTORATION_NATIVE_CAMERA_OBSERVERS=1)
dawn_compile(fireteam_bootstrap_camera_native SOURCES ${FIRETEAM_BOOTSTRAP}
    DEFINES RESTORATION_NATIVE_CAMERA_OBSERVERS=1 RESTORATION_NATIVE_FIRETEAM_CALLBACK=1)
dawn_compile(fireteam_callback_dispatch
    SOURCES ${SRC}/steam/runtime/callbacks/callback_dispatch.cpp
    DEFINES RESTORATION_NATIVE_FIRETEAM_CALLBACK_PROBE=1)
dawn_test(dedicated_fireteam_bootstrap_policy_test
    ${TESTS}/dedicated_fireteam_bootstrap_policy_test.cpp)
dawn_test(native_fireteam_callback_policy_test ${TESTS}/native_fireteam_callback_policy_test.cpp)
dawn_test(world_presence_lifetime_test ${TESTS}/world_presence_lifetime_test.cpp)
dawn_test(tower_search_completion_policy_test ${TESTS}/tower_search_completion_policy_test.cpp)
dawn_test(bootflow_shutdown_contract_test ${TESTS}/bootflow_shutdown_contract_test.cpp
    SOURCES ${SRC}/client/hooks/bootflow/bootflow_hook_lifecycle.cpp)
dawn_test(central_gameplay_bootstrap_contract_test
    ${TESTS}/central_gameplay_bootstrap_contract_test.cpp
    SOURCES ${SRC}/middleware/gameplay/descriptor/join_descriptor.cpp)
dawn_test(initial_lobby_bootstrap_policy_test ${TESTS}/initial_lobby_bootstrap_policy_test.cpp
    SOURCES ${SRC}/middleware/social/realtime_codec.cpp)

dawn_compile(orbit_slice_set_compat SOURCES ${SRC}/client/hooks/bootflow/orbit_slice_set.cpp)
dawn_compile(orbit_slice_set_native SOURCES ${SRC}/client/hooks/bootflow/orbit_slice_set.cpp
    DEFINES RESTORATION_NATIVE_ORBIT_SLICE_SET=1)
foreach(trust 0 1)
    dawn_compile(package_trust_${trust}
        SOURCES ${SRC}/client/hooks/package_trust/package_trust_bypass.cpp
        DEFINES RESTORATION_NATIVE_PACKAGE_TRUST=${trust}
                RESTORATION_NATIVE_PACKAGE_EXTENDED_HASH=1
                RESTORATION_NATIVE_PACKAGE_CACHED_HASH=1)
    dawn_compile(package_trust_orbit_${trust}
        SOURCES ${SRC}/client/hooks/bootflow/orbit_slice_set.cpp
        DEFINES RESTORATION_NATIVE_PACKAGE_TRUST=${trust})
endforeach()

# --- content and sign-on

dawn_test(native_content_bootstrap_policy_test ${TESTS}/native_content_bootstrap_policy_test.cpp
    SOURCES ${SRC}/client/content/bootstrap/bootstrap_token_publish.cpp
            ${SRC}/core/identity/device_identity.cpp
            ${SRC}/core/filesystem/path.cpp
    DEFINES RESTORATION_UNIVERSAL_CLIENT=1
    LIBS bcrypt crypt32)
dawn_test(native_content_quiescence_test ${TESTS}/native_content_quiescence_test.cpp
    SOURCES ${SRC}/client/hooks/network/content_config/content_native_quiescence.cpp
            ${SRC}/client/hooks/network/content_config/gate/content_config_signature_gate.cpp)
dawn_compile(content_signon_client
    SOURCES ${SRC}/client/content/bootstrap/bootstrap_token_publish.cpp
            ${SRC}/server/http/server_http.cpp
    DEFINES RESTORATION_UNIVERSAL_CLIENT=1)
dawn_compile(content_signon_server SOURCES ${SERVER}/account_http.cpp
    DEFINES RESTORATION_DEDICATED_SERVER=1 INCLUDES ${SERVER})
dawn_test(native_content_signon_producer_test ${TESTS}/native_content_signon_producer_test.cpp
    SOURCES ${SRC}/middleware/signon/config/signon_config_blob.cpp
            ${SRC}/server/http/content_configuration.cpp
            ${SRC}/core/identity/device_identity.cpp
            ${SRC}/core/filesystem/path.cpp
    LIBS bcrypt crypt32
    ARGS native-content-signon-producer-fixture.bin)
dawn_test(native_signon_config_test ${TESTS}/native_signon_config_test.cpp
    SOURCES ${SIGNON} ${SRC}/state/entitlements/entitlement_validation.cpp
    ARGS native-signon-production-config.bin)
dawn_test(native_curl_guard_contract_test ${TESTS}/native_curl_guard_contract_test.cpp
    SOURCES ${SRC}/client/hooks/external_server/external_server_setopt_guard.cpp
            ${SRC}/client/hooks/external_server/external_server_route.cpp
            ${SRC}/client/hooks/network/network_call_quiescence.cpp
            ${SRC}/client/hooks/network/content_config/gate/content_config_signature_gate.cpp
            ${SRC}/client/hooks/network/content_config/lifecycle/content_config_hook_storage.cpp
            ${SRC}/client/hooks/network/content_config/lifecycle/content_config_hook_lifecycle.cpp
            ${SRC}/client/hooks/network/content_config/lifecycle/content_config_hook_uninstall.cpp
    LIBS synchronization)
dawn_test(native_url_diagnostic_test ${TESTS}/native_url_diagnostic_test.cpp FROM_ROOT)
dawn_compile(external_server_setopt_guard
    SOURCES ${SRC}/client/hooks/external_server/external_server_setopt_guard.cpp)
dawn_test(external_server_deadline_test ${TESTS}/external_server_deadline_test.cpp LIBS ws2_32)

# --- crucible and pvp

dawn_test(crucible_native_maps_contract_test ${TESTS}/crucible_native_maps_contract_test.cpp
    SOURCES ${SRC}/state/activity/crucible/native_maps.cpp)
dawn_test(crucible_match_host_test ${TESTS}/crucible_match_host_test.cpp
    SOURCES ${SRC}/state/activity/crucible/match_rules.cpp)
dawn_test(crucible_match_rules_test ${TESTS}/crucible_match_rules_test.cpp
    SOURCES ${SRC}/state/activity/crucible/match_rules.cpp)
dawn_test(private_match_options_test ${TESTS}/private_match_options_test.cpp
    SOURCES ${SRC}/state/activity/crucible/match_rules.cpp
            ${SRC}/middleware/bap/activity_host_manager/request/selection/activity_manager_descriptor_parser.cpp
            ${SRC}/middleware/bap/activity_host_manager/request/selection/activity_manager_descriptor_reader.cpp
            ${SRC}/middleware/encoding/bit_reader.cpp
            ${SRC}/middleware/encoding/bit_writer.cpp)
dawn_test(native_match_ui_test ${TESTS}/native_match_ui_test.cpp)
dawn_test(native_pvp_query_component_test ${TESTS}/native_pvp_query_component_test.cpp)
dawn_test(combat_lifecycle_weapon_test ${TESTS}/combat_lifecycle_weapon_test.cpp)
dawn_test(special_ammo_handoff_test ${TESTS}/special_ammo_handoff_test.cpp)
foreach(observer 0 1)
    foreach(trace 0 1)
        set(v ${observer}_trace${trace})
        dawn_test(pvp_all_ammo_${v} ${TESTS}/native_pvp_all_ammo_omission_test.cpp
            DEFINES RESTORATION_NATIVE_AMMO_HANDOFF_TRIAL=1
                    RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL=${observer}
                    RESTORATION_HOOK_REDUCTION_TRACE=${trace})
        dawn_test(pvp_ammo_observer_trial_${v} ${TESTS}/native_pvp_ammo_observer_trial_test.cpp
            DEFINES SELF_TRIAL=${observer}
                    RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL=${observer}
                    RESTORATION_HOOK_REDUCTION_TRACE=${trace})
        dawn_test(pvp_ammo_observer_lifecycle_${v}
            ${TESTS}/native_pvp_ammo_observer_lifecycle_test.cpp
            DEFINES RESTORATION_NATIVE_AMMO_OBSERVER_TRIAL=${observer}
                    RESTORATION_HOOK_REDUCTION_TRACE=${trace})
    endforeach()
endforeach()
foreach(combat 0 1)
    foreach(presentation 0 1)
        dawn_compile(multiplayer_trial_${combat}_${presentation}
            SOURCES ${SRC}/client/hooks/bootflow/multiplayer_hook_lifecycle.cpp
            DEFINES RESTORATION_HOOK_REDUCTION_TRACE=1
                    RESTORATION_NATIVE_COMBAT_PRIORITY=${combat}
                    RESTORATION_NATIVE_MATCH_PRESENTATION=${presentation})
    endforeach()
endforeach()
foreach(pending 0 1)
    foreach(apply 0 1)
        dawn_test(weapon_omission_trial_${pending}${apply}
            ${TESTS}/native_weapon_omission_trial_test.cpp
            DEFINES RESTORATION_NATIVE_WEAPON_PENDING_OMISSION=${pending}
                    RESTORATION_NATIVE_WEAPON_APPLY_OMISSION=${apply})
    endforeach()
endforeach()
foreach(trace 0 1)
    dawn_test(weapon_shot_diagnostic_${trace} ${TESTS}/native_weapon_shot_diagnostic_test.cpp
        DEFINES RESTORATION_NATIVE_WEAPON_PENDING_OMISSION=1
                RESTORATION_NATIVE_WEAPON_APPLY_OMISSION=1
                RESTORATION_REMOTE_WEAPON_SHOT_TRACE=${trace})
    dawn_test(facing_diagnostic_${trace} ${TESTS}/native_facing_diagnostic_test.cpp
        DEFINES RESTORATION_REMOTE_FACING_TRACE=${trace})
    dawn_compile(multiplayer_facing_${trace}
        SOURCES ${SRC}/client/hooks/bootflow/multiplayer_hook_lifecycle.cpp
        DEFINES RESTORATION_REMOTE_FACING_TRACE=${trace})
endforeach()
dawn_test(remote_facing_control_test ${TESTS}/remote_facing_control_test.cpp)

# --- weapons

dawn_test(weapon_roll_policy_test ${TESTS}/weapon_roll_policy_test.cpp)
dawn_test(weapon_acquire_request_test ${TESTS}/weapon_acquire_request_test.cpp)
dawn_test(weapon_acquisition_feedback_test ${TESTS}/weapon_acquisition_feedback_test.cpp)
dawn_test(weapon_power_projection_test ${TESTS}/weapon_power_projection_test.cpp
    SOURCES ${SRC}/middleware/datagen/character_record/appearance/character_appearance_stats.cpp)

file(GLOB CACHE_RECORDS ${SRC}/state/build_data/cache/records/*.cpp)
file(GLOB_RECURSE CACHE_CATALOGS ${SRC}/state/build_data/*catalog.cpp)
dawn_test(weapon_power_cache_loader_test ${TESTS}/weapon_power_cache_loader_test.cpp
    SOURCES ${SRC}/state/build_data/cache/read/cache_file_reader.cpp
            ${SRC}/state/build_data/cache/read/cache_payload_reader.cpp
            ${SRC}/core/filesystem/path.cpp
            ${CACHE_RECORDS} ${CACHE_CATALOGS}
    NO_RUN)
if(DAWN_BUILD_DATA)
    add_test(NAME weapon_power_cache_loader_test
        COMMAND weapon_power_cache_loader_test ${DAWN_BUILD_DATA})
endif()

# --- entities

dawn_test(entity_creation_fairness_test ${TESTS}/entity_creation_fairness_test.cpp)
dawn_test(entity_handoff_fairness_test ${TESTS}/entity_handoff_fairness_test.cpp)
dawn_test(entity_movement_fairness_test ${TESTS}/entity_movement_fairness_test.cpp)
dawn_test(public_elastic_slot_policy_test ${TESTS}/public_elastic_slot_policy_test.cpp
    SOURCES ${SRC}/state/activity/shared/runtime.cpp
            ${SRC}/state/activity/entity_slots/transactions/activity_entity_slot_prepare.cpp
            ${SRC}/state/activity/entity_slots/transactions/activity_entity_slot_commit.cpp
            ${SRC}/state/activity/entity_retirement/entity_retirement_runtime.cpp)
# the slot tables are large stack objects
target_link_options(public_elastic_slot_policy_test PRIVATE /STACK:16777216)
dawn_test(public_membership_binding_test ${TESTS}/public_membership_binding_test.cpp)
dawn_compile(public_membership_binding
    SOURCES ${SRC}/server/gameplay/group/group_host.cpp ${SRC}/state/activity/shared/runtime.cpp
    DEFINES RESTORATION_DEDICATED_SERVER)

# --- gameplay transport

dawn_test(gameplay_acknowledgement_test ${TESTS}/gameplay_acknowledgement_test.cpp
    SOURCES ${SRC}/middleware/gameplay/peer/established_packet.cpp ${BIT_CODEC})
dawn_test(gameplay_central_lobby_identity_contract_test
    ${TESTS}/gameplay_central_lobby_identity_contract_test.cpp)
dawn_test(gameplay_group_orbit_parameters_contract_test
    ${TESTS}/gameplay_group_orbit_parameters_contract_test.cpp
    SOURCES ${SRC}/middleware/gameplay/group/parameter_messages.cpp ${BIT_CODEC})
dawn_test(gameplay_parameter_authority_contract_test
    ${TESTS}/gameplay_parameter_authority_contract_test.cpp
    SOURCES ${SRC}/middleware/gameplay/group/parameter_messages.cpp ${BIT_CODEC})
dawn_test(gameplay_membership_no_profile_contract_test
    ${TESTS}/gameplay_membership_no_profile_contract_test.cpp SOURCES ${GROUP_SESSION})
dawn_test(gameplay_membership_profile_contract_test
    ${TESTS}/gameplay_membership_profile_contract_test.cpp
    SOURCES ${SRC}/middleware/gameplay/group/member_messages.cpp ${GROUP_SESSION})
dawn_test(gameplay_reliable_message_boundary_contract_test
    ${TESTS}/gameplay_reliable_message_boundary_contract_test.cpp
    SOURCES ${SRC}/middleware/gameplay/peer/established_packet.cpp
            ${SRC}/middleware/gameplay/peer/reliable_assembly.cpp ${BIT_CODEC})
dawn_test(gameplay_view_session_route_contract_test
    ${TESTS}/gameplay_view_session_route_contract_test.cpp)
dawn_test(inbound_ack_context_test ${TESTS}/inbound_ack_context_test.cpp)
dawn_test(native_packet_stamp_test ${TESTS}/native_packet_stamp_test.cpp)
dawn_test(packet_fragments_test ${TESTS}/packet_fragments_test.cpp)
dawn_test(native_view_message_test ${TESTS}/native_view_message_test.cpp
    SOURCES ${SRC}/middleware/gameplay/group/view_message.cpp ${BIT_CODEC})
dawn_test(owner_gameplay_admission_contract_test ${TESTS}/owner_gameplay_admission_contract_test.cpp)
dawn_test(rendezvous_batch_test ${TESTS}/rendezvous_batch_test.cpp
    SOURCES ${SRC}/middleware/social/realtime_codec.cpp)
dawn_test(retail_multiplayer_capture_test ${TESTS}/retail_multiplayer_capture_test.cpp)

# --- network hooks

dawn_test(egress_hook_surface_test ${TESTS}/egress_hook_surface_test.cpp
    SOURCES ${SRC}/core/runtime/host_environment.cpp
            ${SRC}/client/hooks/egress/lifecycle/egress_guard_exports.cpp
            ${SRC}/client/hooks/egress/lifecycle/egress_guard_modules.cpp
            ${SRC}/client/hooks/egress/policy/egress_policy_endpoint.cpp
            ${SRC}/client/hooks/egress/winsock/connection/egress_connection_replacements.cpp
            ${SRC}/client/hooks/egress/winsock/discovery/egress_discovery_responder.cpp
            ${SRC}/client/hooks/egress/winsock/transmission/egress_datagram_transmission.cpp
            ${SRC}/client/hooks/egress/winsock/transmission/egress_message_transmission.cpp
            ${SRC}/client/hooks/egress/winsock/transmission/egress_transmission_replacements.cpp
            ${SRC}/client/hooks/egress/resolver/egress_resolver_forward.cpp
            ${SRC}/client/hooks/egress/resolver/egress_resolver_reverse.cpp
            ${SRC}/client/hooks/egress/dns/egress_dns_replacements.cpp
            ${SRC}/client/hooks/egress/extensions/egress_extension_replacements.cpp
            ${SRC}/client/hooks/egress/winsock/reception/egress_reception_replacements.cpp
            ${SRC}/client/hooks/egress/winsock/control/egress_control_replacement.cpp
            ${VENDOR}/detours/detours.cpp
            ${VENDOR}/detours/disasm.cpp
            ${VENDOR}/detours/modules.cpp
    LIBS ws2_32 mswsock dnsapi)
dawn_test(egress_native_sdr_hook_test ${TESTS}/egress_native_sdr_hook_test.cpp
    SOURCES ${SRC}/client/hooks/egress/policy/egress_policy_endpoint.cpp
            ${SRC}/client/hooks/egress/winsock/transmission/egress_datagram_transmission.cpp
            ${SRC}/client/hooks/egress/winsock/transmission/egress_message_transmission.cpp
            ${SRC}/client/hooks/egress/winsock/transmission/egress_transmission_replacements.cpp
    LIBS ws2_32)
dawn_test(egress_policy_endpoint_test ${TESTS}/egress_policy_endpoint_test.cpp
    SOURCES ${SRC}/client/hooks/egress/policy/egress_policy_endpoint.cpp
    LIBS ws2_32)
dawn_test(detour_failure_telemetry_test ${TESTS}/detour_failure_telemetry_test.cpp
    INCLUDES ${VENDOR}/detours)
dawn_test(detour_thread_race_test ${TESTS}/detour_thread_race_test.cpp INCLUDES ${VENDOR}/detours)
dawn_compile(detour_thread_transaction
    SOURCES ${SRC}/client/hooking/detour/transaction/detour_thread_transaction.cpp
    INCLUDES ${VENDOR}/detours)

dawn_test(native_network_diagnostics_test ${TESTS}/native_network_diagnostics_test.cpp)
dawn_test(native_server_endpoint_parser_test ${TESTS}/native_server_endpoint_parser_test.cpp
    SOURCES ${SRC}/core/settings/server/gameplay/gameplay_settings_validation.cpp
            ${SRC}/core/settings/address_text.cpp
            ${SRC}/state/entitlements/entitlement_validation.cpp
            ${SRC}/state/entitlements/entitlement_defaults.cpp)

# The route producer test loads a small DLL standing in for the game's discovery module.
add_library(native_discovery_image SHARED ${TESTS}/native_discovery_image.cpp)
target_compile_options(native_discovery_image PRIVATE /W4 /WX)
dawn_test(native_network_route_producer_test ${TESTS}/native_network_route_producer_test.cpp
    SOURCES ${SRC}/core/settings/address_text.cpp
            ${SRC}/client/network/native_routes/discovery_runtime.cpp
            ${SRC}/state/session/session_runtime.cpp
            ${SRC}/state/entitlements/entitlement_validation.cpp
            ${SIGNON}
            ${SRC}/middleware/bap/activity_host/activity_host_response.cpp
            ${SRC}/middleware/bap/activity_host_manager/response/activity_manager_response.cpp
            ${SRC}/middleware/protobuf/protobuf_reader.cpp
            ${SRC}/middleware/protobuf/protobuf_wire.cpp
            ${SRC}/middleware/gameplay/group/parameter_messages.cpp
            ${BIT_CODEC}
    LIBS ws2_32 bcrypt
    ARGS $<TARGET_FILE:native_discovery_image>)

dawn_test(network_hook_reduction_test ${TESTS}/network_hook_reduction_test.cpp
    SOURCES ${SRC}/client/hooks/network/lifecycle/network_hook_entries.cpp
            ${SRC}/client/hooks/network/platform.cpp
    NO_RUN)
foreach(run "0;0;" "0;1;probe" "1;1;probe authentication" "1;0;")
    list(GET run 0 auth)
    list(GET run 1 cert)
    list(GET run 2 args)
    separate_arguments(args)
    add_test(NAME network_hook_reduction_auth${auth}_cert${cert}
        COMMAND network_hook_reduction_test ${args})
    set_tests_properties(network_hook_reduction_auth${auth}_cert${cert} PROPERTIES ENVIRONMENT
        "DAWN_NATIVE_STEAM_AUTHENTICATION=${auth};DAWN_NATIVE_STEAM_CERTIFICATE=${cert}")
endforeach()
dawn_test(network_startup_dependency_test ${TESTS}/network_startup_dependency_test.cpp
    SOURCES ${SRC}/client/hooks/network/lifecycle/network_hook_lifecycle.cpp)

dawn_test(native_certificate_producer_test ${TESTS}/native_certificate_producer_test.cpp
    SOURCES ${SRC}/server/transport/native_certificate_cache.cpp
            ${SRC}/middleware/bap/certificate.cpp
            ${PROTOBUF} ${SRC}/middleware/protobuf/protobuf_measure.cpp)
dawn_test(native_certificate_publish_test ${TESTS}/native_certificate_publish_test.cpp
    SOURCES ${PROTOBUF} ${SRC}/middleware/protobuf/protobuf_measure.cpp
    DEFINES RESTORATION_UNIVERSAL_CLIENT=1)
dawn_compile(certificate_publish_client
    SOURCES ${SRC}/steam/runtime/transport/native_certificate_publish.cpp
    DEFINES RESTORATION_UNIVERSAL_CLIENT=1)
dawn_compile(certificate_publish_server
    SOURCES ${SRC}/steam/runtime/transport/native_certificate_publish.cpp
            ${SRC}/server/bap/encrypted/body/bap_service_body.cpp
    DEFINES RESTORATION_DEDICATED_SERVER=1)

# --- queuez and subscriptions

dawn_test(inline_subscription_consume_test ${TESTS}/inline_subscription_consume_test.cpp
    SOURCES ${SRC}/server/bap/encrypted/routing/bap_service_routing.cpp
            ${SRC}/server/bap/encrypted/reply/bap_encrypted_reply.cpp
            ${SRC}/middleware/bap/bap_frame.cpp
            ${SRC}/middleware/secure_channel/encrypted_frame.cpp
            ${WEB_SERVICE}
            ${SRC}/middleware/web_service/messages/opcode206_codec.cpp
            ${SRC}/middleware/encoding/bit_reader.cpp
            ${SRC}/middleware/encoding/bit_writer.cpp
            ${SRC}/middleware/queuez/queuez_update.cpp
            ${QUEUEZ_STAGING}
            ${SRC}/server/web_service/opcode_routes.cpp
    LIBS bcrypt
    NO_RUN)
foreach(mode 0 1)
    add_test(NAME inline_subscription_consume_${mode}
        COMMAND inline_subscription_consume_test ${mode})
endforeach()
dawn_test(inline_subscription_response_test ${TESTS}/inline_subscription_response_test.cpp
    SOURCES ${WEB_SERVICE}
            ${SRC}/middleware/encoding/bit_reader.cpp
            ${SRC}/middleware/encoding/bit_writer.cpp
            ${SRC}/middleware/queuez/queuez_update.cpp
    ARGS inline-subscription-response.bin)
dawn_compile(queuez_server
    SOURCES ${SRC}/server/bap/encrypted/encrypted_runtime.cpp
            ${SRC}/server/bap/encrypted/queuez/queuez_outcome_staging.cpp
            ${SRC}/server/bap/encrypted/push/queuez/queuez_subscription.cpp
    DEFINES RESTORATION_DEDICATED_SERVER=1)

dawn_test(native_subscription_policy_test ${TESTS}/native_subscription_policy_test.cpp NO_RUN)
dawn_test(native_subscription_compiled_policy_test ${TESTS}/native_subscription_policy_test.cpp
    DEFINES RESTORATION_NATIVE_SUBSCRIPTION_RESPONSES=1 NO_RUN)
foreach(run "native_subscription_policy_test;-;0" "native_subscription_policy_test;1;1"
        "native_subscription_policy_test;0;0" "native_subscription_compiled_policy_test;-;1"
        "native_subscription_compiled_policy_test;0;0")
    list(JOIN run "_" name)
    string(REPLACE "-" "x" name ${name})
    add_test(NAME ${name} COMMAND ${run})
endforeach()
dawn_test(native_subscription_fallback_test ${TESTS}/native_subscription_fallback_test.cpp
    SOURCES ${QUEUEZ_STAGING} ${WEB_SERVICE} ${SRC}/middleware/encoding/bit_writer.cpp)
dawn_test(native_subscription_progress_test ${TESTS}/native_subscription_progress_test.cpp
    SOURCES ${QUEUEZ_STAGING} ${SRC}/middleware/queuez/queuez_update.cpp)
dawn_test(opcode601_completion_test ${TESTS}/opcode601_completion_test.cpp
    SOURCES ${SRC}/middleware/web_service/messages/opcode601/opcode601_codec.cpp
            ${SRC}/middleware/web_service/status_fields.cpp
            ${SRC}/middleware/encoding/bit_reader.cpp
            ${SRC}/middleware/encoding/bit_writer.cpp)
dawn_test(queuez_family1_public_profile_contract_test
    ${TESTS}/queuez_family1_public_profile_contract_test.cpp
    SOURCES ${SRC}/middleware/datagen/family1/family1_encoder.cpp)
dawn_test(queuez_family6_graph_contract_test ${TESTS}/queuez_family6_graph_contract_test.cpp
    SOURCES ${SRC}/middleware/datagen/family6/family6_encoder.cpp
            ${SRC}/middleware/datagen/social_roster/social_roster_encoder.cpp)
dawn_test(queuez_family7_join_contract_test ${TESTS}/queuez_family7_join_contract_test.cpp
    SOURCES ${SRC}/middleware/datagen/family7/family7_encoder.cpp
            ${SRC}/middleware/gameplay/descriptor/join_descriptor.cpp)
dawn_test(queuez_null_producer_default ${TESTS}/queuez_null_producer_trace_test.cpp)
dawn_test(queuez_null_producer_trace ${TESTS}/queuez_null_producer_trace_test.cpp
    DEFINES RESTORATION_QUEUEZ_NULL_PRODUCER_TRACE=1)
dawn_test(social_roster_group_key_contract_test ${TESTS}/social_roster_group_key_contract_test.cpp
    SOURCES ${SRC}/middleware/datagen/social_roster/social_roster_encoder.cpp)

# --- steam

dawn_test(steam_call_result_routing_contract_test
    ${TESTS}/steam_call_result_routing_contract_test.cpp)
dawn_test(steam_managed_session_policy_test ${TESTS}/steam_managed_session_policy_test.cpp)
dawn_test(steam_social_delta_contract_test ${TESTS}/steam_social_delta_contract_test.cpp)

# --- input, cursor and window

dawn_test(hud_status_world_presence_test ${TESTS}/hud_status_world_presence_test.cpp
    INCLUDES ${VENDOR}/imgui FROM_ROOT)
dawn_test(native_bitmap_omission_test ${TESTS}/native_bitmap_omission_test.cpp
    DEFINES RESTORATION_BITMAP_GUARD_OMISSION=1 FROM_ROOT)
dawn_test(native_camera_observer_omission_test ${TESTS}/native_camera_observer_omission_test.cpp
    FROM_ROOT)
dawn_test(native_cell_abandon_test ${TESTS}/native_cell_abandon_test.cpp
    SOURCES ${SRC}/middleware/encoding/bit_reader.cpp FROM_ROOT)
dawn_test(native_player_observer_test ${TESTS}/native_player_observer_test.cpp FROM_ROOT)

foreach(o 0 1)
    dawn_test(native_async_omission_${o} ${TESTS}/native_async_omission_test.cpp
        DEFINES RESTORATION_NATIVE_ASYNC_OMISSION=${o} FROM_ROOT)
    dawn_test(native_cursor_omission_${o} ${TESTS}/native_cursor_omission_test.cpp
        DEFINES RESTORATION_NATIVE_SET_CURSOR_OMISSION=${o} FROM_ROOT)
    dawn_test(native_cursor_pending_tail_${o} ${TESTS}/native_cursor_pending_tail_test.cpp
        SOURCES ${CURSOR}/native_cursor_pending_tail.cpp
        DEFINES RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL=${o} FROM_ROOT)
    dawn_test(native_cursor_simple_tail_${o} ${TESTS}/native_cursor_simple_tail_test.cpp
        SOURCES ${CURSOR}/native_cursor_simple_tail.cpp
        DEFINES RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL=${o} FROM_ROOT)
    dawn_compile(cursor_tail_adapter_${o}
        SOURCES ${CURSOR}/native_cursor_tail_runtime.cpp
                ${CURSOR}/native_cursor_pending_tail.cpp
                ${CURSOR}/cursor_hook_lifecycle.cpp
                ${CURSOR}/cursor_guard_replacements.cpp
                ${GRAPHICS}/input/graphics_window_input.cpp
                ${GRAPHICS}/input/graphics_raw_input_window.cpp
        DEFINES RESTORATION_NATIVE_CLIP_OMISSION=${o}
                RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL=${o}
                RESTORATION_NATIVE_RAW_MOUSE_PAUSE=1
                RESTORATION_NATIVE_SET_CURSOR_OMISSION=1)
    dawn_compile(polled_input_${o}
        SOURCES ${SRC}/client/hooks/polled_input/polled_input_replacements.cpp
        DEFINES RESTORATION_NATIVE_INPUT_PAUSE=1 RESTORATION_NATIVE_KEY_FOCUS_OMISSION=${o})
    dawn_compile(polled_input_lifecycle_${o}
        SOURCES ${SRC}/client/hooks/polled_input/polled_input_lifecycle.cpp
        DEFINES RESTORATION_NATIVE_ASYNC_OMISSION=1 RESTORATION_NATIVE_KEY_FOCUS_OMISSION=${o})
    dawn_test(raw_input_capture_claim_${o} ${TESTS}/raw_input_capture_claim_test.cpp
        SOURCES ${SRC}/core/ui/runtime/ui_visibility_runtime.cpp
        DEFINES RESTORATION_NATIVE_RAW_MOUSE_PAUSE=${o}
        LIBS user32 FROM_ROOT)
    dawn_compile(raw_input_window_${o}
        SOURCES ${GRAPHICS}/input/graphics_raw_input_window.cpp
        DEFINES RESTORATION_NATIVE_RAW_MOUSE_PAUSE=${o})
endforeach()
dawn_test(native_cursor_tail_lifecycle_test ${TESTS}/native_cursor_tail_lifecycle_test.cpp
    DEFINES RESTORATION_NATIVE_CLIP_OMISSION=1 RESTORATION_NATIVE_SET_CURSOR_OMISSION=1 FROM_ROOT)
dawn_test(native_cursor_tail_runtime_test ${TESTS}/native_cursor_tail_runtime_test.cpp
    SOURCES ${CURSOR}/native_cursor_pending_tail.cpp
            ${SRC}/core/ui/runtime/ui_visibility_runtime.cpp
    DEFINES RESTORATION_NATIVE_CLIP_OMISSION=1 RESTORATION_NATIVE_CURSOR_SIMPLE_TAIL=1
    LIBS user32 FROM_ROOT)
dawn_test(native_cursor_shutdown_order_test ${TESTS}/native_cursor_shutdown_order_test.cpp
    LIBS user32 FROM_ROOT)

dawn_test(native_keyboard_state_producer_test ${TESTS}/native_keyboard_state_producer_test.cpp
    LIBS user32 FROM_ROOT)
dawn_compile(keyboard_state_sources
    SOURCES ${SRC}/client/hooks/polled_input/polled_input_replacements.cpp
            ${SRC}/client/hooks/polled_input/polled_input_lifecycle.cpp
            ${GRAPHICS}/input/graphics_window_input.cpp
            ${SRC}/client/hooks/teleport/teleport_lifecycle.cpp
            ${SRC}/client/hooks/teleport/teleport_move.cpp)
dawn_test(native_key_focus_policy_test ${TESTS}/native_key_focus_policy_test.cpp
    LIBS user32 FROM_ROOT)
dawn_test(native_key_focus_source_test ${TESTS}/native_key_focus_source_test.cpp
    LIBS user32 FROM_ROOT)
dawn_test(native_key_guard_recovery_test ${TESTS}/native_key_guard_recovery_test.cpp
    DEFINES RESTORATION_NATIVE_ASYNC_OMISSION=1 RESTORATION_NATIVE_KEY_FOCUS_OMISSION=1
    LIBS user32 FROM_ROOT)

dawn_test(native_raw_mouse_pause_test ${TESTS}/ui_movement_hook_boundary_test.cpp
    SOURCES ${SRC}/client/hooks/assert_handler/assert_handler_lifecycle.cpp
    DEFINES RESTORATION_NATIVE_RAW_MOUSE_PAUSE=1
    LIBS user32 FROM_ROOT)
dawn_test(ui_movement_hook_boundary_test ${TESTS}/ui_movement_hook_boundary_test.cpp
    SOURCES ${SRC}/client/hooks/assert_handler/assert_handler_lifecycle.cpp
    LIBS user32 FROM_ROOT)

dawn_test(native_window_presentation_test ${TESTS}/native_window_presentation_test.cpp
    INCLUDES ${IMGUI_INCLUDES} LIBS user32 FROM_ROOT)
dawn_test(native_draw_callback_policy_test ${TESTS}/native_draw_callback_policy_test.cpp
    SOURCES ${VENDOR}/imgui/imgui.cpp ${VENDOR}/imgui/imgui_draw.cpp
            ${VENDOR}/imgui/imgui_tables.cpp ${VENDOR}/imgui/imgui_widgets.cpp
    INCLUDES ${IMGUI_INCLUDES} FROM_ROOT)
dawn_test(graphics_surface_boundary_test ${TESTS}/graphics_surface_boundary_test.cpp
    INCLUDES ${IMGUI_INCLUDES} LIBS user32 d3d11 dxgi FROM_ROOT)
dawn_test(graphics_transient_target_test ${TESTS}/graphics_surface_boundary_test.cpp
    DEFINES RESTORATION_TRANSIENT_RTV=1
    INCLUDES ${IMGUI_INCLUDES} LIBS user32 d3d11 dxgi FROM_ROOT)
dawn_test(native_graphics_resize_omission_test ${TESTS}/native_graphics_resize_omission_test.cpp
    INCLUDES ${IMGUI_INCLUDES} LIBS user32 NO_RUN)
if(DAWN_NATIVE_TEXT)
    add_test(NAME native_graphics_resize_omission_test
        COMMAND native_graphics_resize_omission_test ${DAWN_NATIVE_TEXT}
        WORKING_DIRECTORY ${ROOT})
endif()

dawn_compile(window_presentation_native
    SOURCES ${GRAPHICS}/native_window_presentation.cpp
    DEFINES RESTORATION_NATIVE_WINDOW_PRESENTATION=1 RESTORATION_NATIVE_RESIZE_OMISSION=1
            RESTORATION_TRANSIENT_RTV=1
    INCLUDES ${IMGUI_INCLUDES})
dawn_compile(window_presentation_default
    SOURCES ${GRAPHICS}/native_window_presentation.cpp
            ${GRAPHICS}/graphics_hook_replacements.cpp
            ${GRAPHICS}/input/graphics_window_input.cpp
            ${SRC}/steam/runtime/callbacks/callback_dispatch.cpp
    INCLUDES ${IMGUI_INCLUDES})
dawn_compile(graphics_lifecycle_resize
    SOURCES ${GRAPHICS}/graphics_hook_lifecycle.cpp
    DEFINES RESTORATION_NATIVE_RESIZE_OMISSION=1 RESTORATION_TRANSIENT_RTV=1
    INCLUDES ${IMGUI_INCLUDES})
dawn_compile(graphics_transient_target
    SOURCES ${GRAPHICS}/graphics_hook_replacements.cpp
            ${GRAPHICS}/renderer/graphics_renderer_device.cpp
            ${GRAPHICS}/renderer/graphics_renderer_lifecycle.cpp
            ${GRAPHICS}/renderer/graphics_renderer_frame.cpp
            ${GRAPHICS}/renderer/selection/graphics_swap_chain_selection.cpp
    DEFINES RESTORATION_TRANSIENT_RTV=1
    INCLUDES ${IMGUI_INCLUDES})

# --- release tooling

dawn_test(native_endpoint_contract_test ${ROOT}/tools/release/native_endpoint_contract_test.cpp
    SOURCES ${SRC}/core/settings/address_text.cpp)

# --- source checks and Python tools

foreach(check family6liverefreshcontract gameplaygrouporbithostownership
        gameplaysharedfireteamcontract nativeactivitylaunchcontract)
    add_test(NAME source_${check}
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
                ${ROOT}/scripts/test-${check}.ps1)
endforeach()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    foreach(script tests/test_private_config.py
            tools/crucible/test_install_native_candidate.py
            tools/crucible/test_localized_strings.py
            tools/crucible/test_stage_native_content.py
            tools/release/test_matched_release.py)
        get_filename_component(name ${script} NAME_WE)
        get_filename_component(dir ${ROOT}/${script} DIRECTORY)
        add_test(NAME py_${name} COMMAND Python3::Interpreter ${ROOT}/${script}
            WORKING_DIRECTORY ${dir})
    endforeach()
endif()
