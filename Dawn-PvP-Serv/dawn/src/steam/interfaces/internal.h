#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

#include "../runtime/runtime.h"

namespace dawn::steam::interfaces {

inline constexpr std::size_t kQuaternionComponents = 4;
inline constexpr std::size_t kVectorComponents = 3;

#pragma pack(push, 1)
struct InputDigitalActionData {
    bool state{};
    bool active{};
};

struct InputAnalogActionData {
    int mode{};
    float x{};
    float y{};
    bool active{};
};

struct InputMotionData {
    float rotation[kQuaternionComponents]{};
    float acceleration[kVectorComponents]{};
    float velocity[kVectorComponents]{};
};
#pragma pack(pop)

struct SteamId {
    std::uint64_t value{};
};

namespace versions {

inline constexpr char kApps[] = "STEAMAPPS_INTERFACE_VERSION008";
inline constexpr char kInput[] = "SteamInput001";
inline constexpr char kUtils[] = "SteamUtils009";
inline constexpr char kFriends[] = "SteamFriends017";
inline constexpr char kUser[] = "SteamUser020";
inline constexpr char kUserStats[] = "STEAMUSERSTATS_INTERFACE_VERSION011";
inline constexpr char kMatchmaking[] = "SteamMatchMaking009";
inline constexpr char kLegacyClient[] = "SteamClient018";
inline constexpr char kSerializedNetworking[] = "SteamNetworkingSocketsSerialized003";
inline constexpr char kHttp[] = "STEAMHTTP_INTERFACE_VERSION003";

} // namespace versions

namespace tables {

[[nodiscard]] bool initialize() noexcept;
[[nodiscard]] void* apps() noexcept;
[[nodiscard]] void* input() noexcept;
[[nodiscard]] void* utils() noexcept;
[[nodiscard]] void* friends() noexcept;
[[nodiscard]] void* user() noexcept;
[[nodiscard]] void* user_stats() noexcept;
[[nodiscard]] void* matchmaking() noexcept;
[[nodiscard]] void* client() noexcept;
[[nodiscard]] void* serialized_networking() noexcept;
[[nodiscard]] void* http() noexcept;

} // namespace tables

namespace methods {

ULONG_PTR empty(void*) noexcept;
bool return_true(void*) noexcept;
const char* persona_name(void*) noexcept;
int persona_state(void*) noexcept;
int get_friend_count(void*, int friendFlags) noexcept;
SteamId* get_friend_by_index(void*, SteamId*, int friendIndex, int friendFlags) noexcept;
int friend_relationship(void*, std::uint64_t friendSteamId) noexcept;
int friend_persona_state(void*, std::uint64_t friendSteamId) noexcept;
const char* friend_persona_name(void*, std::uint64_t friendSteamId) noexcept;
bool friend_game_played(void*, std::uint64_t friendSteamId, void* gameInfo) noexcept;
const char* friend_persona_name_history(void*, std::uint64_t friendSteamId, int nameIndex) noexcept;
int friend_steam_level(void*, std::uint64_t friendSteamId) noexcept;
const char* player_nickname(void*, std::uint64_t playerSteamId) noexcept;
bool has_friend(void*, std::uint64_t friendSteamId, int friendFlags) noexcept;
int friend_avatar(void*, std::uint64_t friendSteamId) noexcept;
bool request_user_information(void*, std::uint64_t userSteamId, bool requireNameOnly) noexcept;
DWORD user_restrictions(void*) noexcept;
bool set_rich_presence(void*, const char* key, const char* value) noexcept;
void clear_rich_presence(void*) noexcept;
const char* friend_rich_presence(void*, std::uint64_t friendSteamId, const char* key) noexcept;
int friend_rich_presence_key_count(void*, std::uint64_t friendSteamId) noexcept;
const char* friend_rich_presence_key_by_index(void*, std::uint64_t friendSteamId, int keyIndex) noexcept;
void request_friend_rich_presence(void*, std::uint64_t friendSteamId) noexcept;
bool invite_user_to_game(void*, std::uint64_t friendSteamId, const char* connectString) noexcept;
bool set_listen_for_friends_messages(void*, bool interceptEnabled) noexcept;
bool reply_to_friend_message(void*, std::uint64_t friendSteamId, const char* message) noexcept;
const char* language(void*) noexcept;
const char* country(void*) noexcept;
std::uint32_t server_real_time(void*) noexcept;
DWORD get_app_id(void*) noexcept;
UserHandle get_user_handle(void*) noexcept;
SteamId* get_steam_id(void*, SteamId* result) noexcept;
SteamId* get_app_owner(void*, SteamId* result) noexcept;
bool app_is_installed(void*, DWORD candidate) noexcept;
bool dlc_installed(void*, DWORD) noexcept;
bool current_beta_name(void*, char*, int) noexcept;
std::uint32_t app_install_dir(void*, DWORD, char*, std::uint32_t) noexcept;
const char* launch_query_param(void*, const char*) noexcept;
int app_build_id(void*) noexcept;
int get_dlc_count(void*) noexcept;
bool get_dlc_data(void*, int, DWORD*, bool*, char*, int) noexcept;
bool request_current_stats(void*) noexcept;
bool get_achievement(void*, const char*, bool*) noexcept;
std::uint64_t input_handle(void*, const char*) noexcept;
InputDigitalActionData input_digital_data(void*, std::uint64_t, std::uint64_t) noexcept;
InputAnalogActionData input_analog_data(void*, std::uint64_t, std::uint64_t) noexcept;
InputMotionData input_motion_data(void*, std::uint64_t) noexcept;
int filter_text(void*, char*, DWORD, const char*, bool) noexcept;
ApiCall create_lobby(void*, int, int) noexcept;
ApiCall join_lobby(void*, std::uint64_t) noexcept;
void leave_lobby(void*, std::uint64_t) noexcept;
bool invite_user_to_lobby(void*, std::uint64_t, std::uint64_t) noexcept;
int get_num_lobby_members(void*, std::uint64_t) noexcept;
SteamId* get_lobby_member_by_index(void*, SteamId*, std::uint64_t, int) noexcept;
const char* get_lobby_data(void*, std::uint64_t, const char*) noexcept;
bool set_lobby_data(void*, std::uint64_t, const char*, const char*) noexcept;
int get_lobby_data_count(void*, std::uint64_t) noexcept;
bool get_lobby_data_by_index(void*, std::uint64_t, int, char*, int, char*, int) noexcept;
bool delete_lobby_data(void*, std::uint64_t, const char*) noexcept;
const char* get_lobby_member_data(void*, std::uint64_t, std::uint64_t, const char*) noexcept;
void set_lobby_member_data(void*, std::uint64_t, const char*, const char*) noexcept;
bool send_lobby_chat(void*, std::uint64_t, const void*, int) noexcept;
int get_lobby_chat_entry(void*, std::uint64_t, int, std::uint64_t*, void*, int, int*) noexcept;
bool request_lobby_data(void*, std::uint64_t) noexcept;
void set_lobby_game_server(void*, std::uint64_t, std::uint32_t, std::uint16_t, std::uint64_t) noexcept;
bool get_lobby_game_server(void*, std::uint64_t, std::uint32_t*, std::uint16_t*, SteamId*) noexcept;
bool set_lobby_member_limit(void*, std::uint64_t, int) noexcept;
int get_lobby_member_limit(void*, std::uint64_t) noexcept;
bool set_lobby_type(void*, std::uint64_t, int) noexcept;
bool set_lobby_joinable(void*, std::uint64_t, bool) noexcept;
SteamId* get_lobby_owner(void*, SteamId*, std::uint64_t) noexcept;
bool set_lobby_owner(void*, std::uint64_t, std::uint64_t) noexcept;
PipeHandle create_steam_pipe(void*) noexcept;
bool release_steam_pipe(void*, PipeHandle pipe) noexcept;
UserHandle connect_to_global_user(void*, PipeHandle pipe) noexcept;
PipeHandle create_local_user(void*, UserHandle* user, int accountType) noexcept;
void release_user(void*, PipeHandle pipe, UserHandle user) noexcept;
void* get_generic_interface(void*, UserHandle, PipeHandle, const char*) noexcept;
void* get_client_user(void*, UserHandle, PipeHandle, const char*) noexcept;
void* get_client_friends(void*, UserHandle, PipeHandle, const char*) noexcept;
void* get_client_matchmaking(void*, UserHandle, PipeHandle, const char*) noexcept;
void* get_client_user_stats(void*, UserHandle, PipeHandle, const char*) noexcept;
void* get_client_apps(void*, UserHandle, PipeHandle, const char*) noexcept;
void* get_client_input(void*, UserHandle, PipeHandle, const char*) noexcept;
void* get_client_utils(void*, PipeHandle, const char*) noexcept;
void* get_client_http(void*, UserHandle, PipeHandle, const char*) noexcept;
int connected_universe(void*) noexcept;
void serialized_send_rendezvous(void*, std::uint64_t, DWORD, const void*, DWORD) noexcept;
void serialized_send_failure(void*, std::uint64_t, DWORD, DWORD, const char*) noexcept;
ApiCall serialized_get_certificate(void*) noexcept;
int serialized_get_network_config(void*, void*, DWORD, const char*) noexcept;
void serialized_cache_relay_ticket(void*, const void*, DWORD) noexcept;
DWORD serialized_relay_ticket_count(void*) noexcept;
int serialized_get_relay_ticket(void*, DWORD, void*, DWORD) noexcept;
void serialized_post_connection_state(void*, const void*, DWORD) noexcept;
ApiCall request_encrypted_app_ticket(void*, const void*, int) noexcept;
bool get_encrypted_app_ticket(void*, void*, int, DWORD*) noexcept;

} // namespace methods
} // namespace dawn::steam::interfaces
