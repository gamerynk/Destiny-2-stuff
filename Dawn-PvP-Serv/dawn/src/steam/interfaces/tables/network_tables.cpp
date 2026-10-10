#include "internal.h"

namespace dawn::steam::interfaces::tables {
namespace {

constexpr std::size_t kMatchmakingMethodCount = 37;
constexpr std::size_t kClientMethodCount = 40;
constexpr std::size_t kSerializedNetworkingMethodCount = 8;
constexpr std::size_t kHttpMethodCount = 1;

enum class MatchmakingSlot : std::size_t {
    createLobby = 13,
    joinLobby = 14,
    leaveLobby = 15,
    inviteUserToLobby = 16,
    lobbyMemberCount = 17,
    lobbyMemberByIndex = 18,
    lobbyData = 19,
    setLobbyData = 20,
    lobbyDataCount = 21,
    lobbyDataByIndex = 22,
    deleteLobbyData = 23,
    lobbyMemberData = 24,
    setLobbyMemberData = 25,
    sendLobbyChat = 26,
    lobbyChatEntry = 27,
    requestLobbyData = 28,
    setLobbyGameServer = 29,
    getLobbyGameServer = 30,
    setLobbyMemberLimit = 31,
    getLobbyMemberLimit = 32,
    setLobbyType = 33,
    setLobbyJoinable = 34,
    lobbyOwner = 35,
    setLobbyOwner = 36,
};

enum class ClientSlot : std::size_t {
    createSteamPipe = 0,
    releaseSteamPipe = 1,
    connectToGlobalUser = 2,
    createLocalUser = 3,
    releaseUser = 4,
    user = 5,
    friends = 8,
    utils = 9,
    matchmaking = 10,
    generic = 12,
    userStats = 13,
    apps = 15,
    http = 24,
    input = 38,
};

enum class SerializedSlot : std::size_t {
    rendezvous = 0,
    failure = 1,
    certificate = 2,
    config = 3,
    cacheTicket = 4,
    ticketCount = 5,
    ticket = 6,
    connectionState = 7,
};

template <SerializedSlot Slot> struct SerializedSignature;

template <> struct SerializedSignature<SerializedSlot::rendezvous> {
    using Type = void (*)(void*, std::uint64_t, DWORD, const void*, DWORD) noexcept;
};

template <> struct SerializedSignature<SerializedSlot::failure> {
    using Type = void (*)(void*, std::uint64_t, DWORD, DWORD, const char*) noexcept;
};

template <> struct SerializedSignature<SerializedSlot::certificate> {
    using Type = ApiCall (*)(void*) noexcept;
};

template <> struct SerializedSignature<SerializedSlot::config> {
    using Type = int (*)(void*, void*, DWORD, const char*) noexcept;
};

template <> struct SerializedSignature<SerializedSlot::cacheTicket> {
    using Type = void (*)(void*, const void*, DWORD) noexcept;
};

template <> struct SerializedSignature<SerializedSlot::ticketCount> {
    using Type = DWORD (*)(void*) noexcept;
};

template <> struct SerializedSignature<SerializedSlot::ticket> {
    using Type = int (*)(void*, DWORD, void*, DWORD) noexcept;
};

template <> struct SerializedSignature<SerializedSlot::connectionState> {
    using Type = void (*)(void*, const void*, DWORD) noexcept;
};

std::array<FARPROC, kMatchmakingMethodCount> g_matchmakingMethods{};
std::array<FARPROC, kClientMethodCount> g_clientMethods{};
std::array<FARPROC, kSerializedNetworkingMethodCount> g_serializedMethods{};
std::array<FARPROC, kHttpMethodCount> g_httpMethods{};
InterfaceObject g_matchmaking{g_matchmakingMethods.data()};
InterfaceObject g_client{g_clientMethods.data()};
InterfaceObject g_serializedNetworking{g_serializedMethods.data()};
InterfaceObject g_http{g_httpMethods.data()};

template <SerializedSlot Slot>
void set_serialized_method(typename SerializedSignature<Slot>::Type function) noexcept {
    set_method(g_serializedMethods[index(Slot)], function);
}

} // namespace

void initialize_networking() noexcept {
    fill_empty(g_matchmakingMethods);
    fill_empty(g_clientMethods);
    fill_empty(g_serializedMethods);
    fill_empty(g_httpMethods);

    set_method(g_matchmakingMethods[index(MatchmakingSlot::createLobby)], &methods::create_lobby);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::joinLobby)], &methods::join_lobby);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::leaveLobby)], &methods::leave_lobby);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::inviteUserToLobby)],
               &methods::invite_user_to_lobby);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyMemberCount)],
               &methods::get_num_lobby_members);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyMemberByIndex)],
               &methods::get_lobby_member_by_index);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyData)],
               &methods::get_lobby_data);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::setLobbyData)],
               &methods::set_lobby_data);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyDataCount)],
               &methods::get_lobby_data_count);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyDataByIndex)],
               &methods::get_lobby_data_by_index);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::deleteLobbyData)],
               &methods::delete_lobby_data);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyMemberData)],
               &methods::get_lobby_member_data);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::setLobbyMemberData)],
               &methods::set_lobby_member_data);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::sendLobbyChat)],
               &methods::send_lobby_chat);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyChatEntry)],
               &methods::get_lobby_chat_entry);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::requestLobbyData)],
               &methods::request_lobby_data);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::setLobbyGameServer)],
               &methods::set_lobby_game_server);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::getLobbyGameServer)],
               &methods::get_lobby_game_server);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::setLobbyMemberLimit)],
               &methods::set_lobby_member_limit);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::getLobbyMemberLimit)],
               &methods::get_lobby_member_limit);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::setLobbyType)],
               &methods::set_lobby_type);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::setLobbyJoinable)],
               &methods::set_lobby_joinable);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::lobbyOwner)],
               &methods::get_lobby_owner);
    set_method(g_matchmakingMethods[index(MatchmakingSlot::setLobbyOwner)],
               &methods::set_lobby_owner);

    set_method(g_clientMethods[index(ClientSlot::createSteamPipe)], &methods::create_steam_pipe);
    set_method(g_clientMethods[index(ClientSlot::releaseSteamPipe)], &methods::release_steam_pipe);
    set_method(g_clientMethods[index(ClientSlot::connectToGlobalUser)],
               &methods::connect_to_global_user);
    set_method(g_clientMethods[index(ClientSlot::createLocalUser)], &methods::create_local_user);
    set_method(g_clientMethods[index(ClientSlot::releaseUser)], &methods::release_user);
    set_method(g_clientMethods[index(ClientSlot::user)], &methods::get_client_user);
    set_method(g_clientMethods[index(ClientSlot::friends)], &methods::get_client_friends);
    set_method(g_clientMethods[index(ClientSlot::utils)], &methods::get_client_utils);
    set_method(g_clientMethods[index(ClientSlot::matchmaking)],
               &methods::get_client_matchmaking);
    set_method(g_clientMethods[index(ClientSlot::generic)], &methods::get_generic_interface);
    set_method(g_clientMethods[index(ClientSlot::userStats)], &methods::get_client_user_stats);
    set_method(g_clientMethods[index(ClientSlot::apps)], &methods::get_client_apps);
    set_method(g_clientMethods[index(ClientSlot::http)], &methods::get_client_http);
    set_method(g_clientMethods[index(ClientSlot::input)], &methods::get_client_input);

    set_serialized_method<SerializedSlot::rendezvous>(&methods::serialized_send_rendezvous);
    set_serialized_method<SerializedSlot::failure>(&methods::serialized_send_failure);
    set_serialized_method<SerializedSlot::certificate>(&methods::serialized_get_certificate);
    set_serialized_method<SerializedSlot::config>(&methods::serialized_get_network_config);
    set_serialized_method<SerializedSlot::cacheTicket>(&methods::serialized_cache_relay_ticket);
    set_serialized_method<SerializedSlot::ticketCount>(&methods::serialized_relay_ticket_count);
    set_serialized_method<SerializedSlot::ticket>(&methods::serialized_get_relay_ticket);
    set_serialized_method<SerializedSlot::connectionState>(
        &methods::serialized_post_connection_state);
}

void* matchmaking() noexcept {
    return &g_matchmaking;
}
void* client() noexcept {
    return &g_client;
}
void* serialized_networking() noexcept {
    return &g_serializedNetworking;
}
void* http() noexcept {
    return &g_http;
}

} // namespace dawn::steam::interfaces::tables
