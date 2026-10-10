#include "internal.h"

namespace dawn::steam::interfaces::tables {
namespace {

constexpr std::size_t kAppsMethodCount = 30;
constexpr std::size_t kInputMethodCount = 35;
constexpr std::size_t kUtilsMethodCount = 34;
constexpr std::size_t kFriendsMethodCount = 80;
constexpr std::size_t kUserMethodCount = 31;
constexpr std::size_t kUserStatsMethodCount = 48;

enum class AppsSlot : std::size_t {
    subscribed = 0,
    currentLanguage = 4,
    availableLanguages = 5,
    subscribedApp = 6,
    dlcInstalled = 7,
    dlcCount = 10,
    dlcData = 11,
    betaName = 15,
    installDir = 18,
    installed = 19,
    owner = 20,
    launchQueryParam = 21,
    buildId = 23,
};

enum class InputSlot : std::size_t {
    initialize = 0,
    shutdown = 1,
    actionSetHandle = 4,
    digitalActionHandle = 11,
    digitalActionData = 12,
    analogActionHandle = 14,
    analogActionData = 15,
    motionData = 20,
};

enum class UtilsSlot : std::size_t {
    universe = 2,
    serverRealTime = 3,
    country = 4,
    appId = 9,
    ipv6Connectivity = 31,
    filterText = 32,
};

enum class FriendsSlot : std::size_t {
    personaName = 0,
    personaState = 2,
    friendCount = 3,
    friendByIndex = 4,
    friendRelationship = 5,
    friendPersonaState = 6,
    friendPersonaName = 7,
    friendGamePlayed = 8,
    friendPersonaNameHistory = 9,
    friendSteamLevel = 10,
    playerNickname = 11,
    hasFriend = 17,
    smallAvatar = 34,
    mediumAvatar = 35,
    largeAvatar = 36,
    requestUserInformation = 37,
    userRestrictions = 42,
    setRichPresence = 43,
    clearRichPresence = 44,
    friendRichPresence = 45,
    friendRichPresenceKeyCount = 46,
    friendRichPresenceKeyByIndex = 47,
    requestFriendRichPresence = 48,
    inviteUserToGame = 49,
    listenForFriendsMessages = 64,
    replyToFriendMessage = 65,
};

enum class UserSlot : std::size_t {
    handle = 0,
    loggedOn = 1,
    steamId = 2,
    requestEncryptedAppTicket = 20,
    getEncryptedAppTicket = 21,
};

enum class UserStatsSlot : std::size_t {
    requestCurrent = 0,
    achievement = 6,
    setAchievement = 7,
    store = 10,
};

std::array<FARPROC, kAppsMethodCount> g_appsMethods{};
std::array<FARPROC, kInputMethodCount> g_inputMethods{};
std::array<FARPROC, kUtilsMethodCount> g_utilsMethods{};
std::array<FARPROC, kFriendsMethodCount> g_friendsMethods{};
std::array<FARPROC, kUserMethodCount> g_userMethods{};
std::array<FARPROC, kUserStatsMethodCount> g_userStatsMethods{};
InterfaceObject g_apps{g_appsMethods.data()};
InterfaceObject g_input{g_inputMethods.data()};
InterfaceObject g_utils{g_utilsMethods.data()};
InterfaceObject g_friends{g_friendsMethods.data()};
InterfaceObject g_user{g_userMethods.data()};
InterfaceObject g_userStats{g_userStatsMethods.data()};

} // namespace

void initialize_common() noexcept {
    fill_empty(g_appsMethods);
    fill_empty(g_inputMethods);
    fill_empty(g_utilsMethods);
    fill_empty(g_friendsMethods);
    fill_empty(g_userMethods);
    fill_empty(g_userStatsMethods);

    set_method(g_appsMethods[index(AppsSlot::subscribed)], &methods::return_true);
    set_method(g_appsMethods[index(AppsSlot::currentLanguage)], &methods::language);
    set_method(g_appsMethods[index(AppsSlot::availableLanguages)], &methods::language);
    set_method(g_appsMethods[index(AppsSlot::subscribedApp)], &methods::return_true);
    set_method(g_appsMethods[index(AppsSlot::dlcInstalled)], &methods::dlc_installed);
    set_method(g_appsMethods[index(AppsSlot::dlcCount)], &methods::get_dlc_count);
    set_method(g_appsMethods[index(AppsSlot::dlcData)], &methods::get_dlc_data);
    set_method(g_appsMethods[index(AppsSlot::betaName)], &methods::current_beta_name);
    set_method(g_appsMethods[index(AppsSlot::installDir)], &methods::app_install_dir);
    set_method(g_appsMethods[index(AppsSlot::installed)], &methods::app_is_installed);
    set_method(g_appsMethods[index(AppsSlot::owner)], &methods::get_app_owner);
    set_method(g_appsMethods[index(AppsSlot::launchQueryParam)], &methods::launch_query_param);
    set_method(g_appsMethods[index(AppsSlot::buildId)], &methods::app_build_id);

    set_method(g_inputMethods[index(InputSlot::initialize)], &methods::return_true);
    set_method(g_inputMethods[index(InputSlot::shutdown)], &methods::return_true);
    set_method(g_inputMethods[index(InputSlot::actionSetHandle)], &methods::input_handle);
    set_method(g_inputMethods[index(InputSlot::digitalActionHandle)], &methods::input_handle);
    set_method(g_inputMethods[index(InputSlot::digitalActionData)], &methods::input_digital_data);
    set_method(g_inputMethods[index(InputSlot::analogActionHandle)], &methods::input_handle);
    set_method(g_inputMethods[index(InputSlot::analogActionData)], &methods::input_analog_data);
    set_method(g_inputMethods[index(InputSlot::motionData)], &methods::input_motion_data);

    set_method(g_utilsMethods[index(UtilsSlot::universe)], &methods::connected_universe);
    set_method(g_utilsMethods[index(UtilsSlot::serverRealTime)], &methods::server_real_time);
    set_method(g_utilsMethods[index(UtilsSlot::country)], &methods::country);
    set_method(g_utilsMethods[index(UtilsSlot::appId)], &methods::get_app_id);
    set_method(g_utilsMethods[index(UtilsSlot::ipv6Connectivity)], &methods::return_true);
    set_method(g_utilsMethods[index(UtilsSlot::filterText)], &methods::filter_text);

    set_method(g_friendsMethods[index(FriendsSlot::personaName)], &methods::persona_name);
    set_method(g_friendsMethods[index(FriendsSlot::personaState)], &methods::persona_state);
    set_method(g_friendsMethods[index(FriendsSlot::friendCount)], &methods::get_friend_count);
    set_method(g_friendsMethods[index(FriendsSlot::friendByIndex)], &methods::get_friend_by_index);
    set_method(g_friendsMethods[index(FriendsSlot::friendRelationship)],
               &methods::friend_relationship);
    set_method(g_friendsMethods[index(FriendsSlot::friendPersonaState)],
               &methods::friend_persona_state);
    set_method(g_friendsMethods[index(FriendsSlot::friendPersonaName)],
               &methods::friend_persona_name);
    set_method(g_friendsMethods[index(FriendsSlot::friendGamePlayed)],
               &methods::friend_game_played);
    set_method(g_friendsMethods[index(FriendsSlot::friendPersonaNameHistory)],
               &methods::friend_persona_name_history);
    set_method(g_friendsMethods[index(FriendsSlot::friendSteamLevel)],
               &methods::friend_steam_level);
    set_method(g_friendsMethods[index(FriendsSlot::playerNickname)],
               &methods::player_nickname);
    set_method(g_friendsMethods[index(FriendsSlot::hasFriend)], &methods::has_friend);
    set_method(g_friendsMethods[index(FriendsSlot::smallAvatar)], &methods::friend_avatar);
    set_method(g_friendsMethods[index(FriendsSlot::mediumAvatar)], &methods::friend_avatar);
    set_method(g_friendsMethods[index(FriendsSlot::largeAvatar)], &methods::friend_avatar);
    set_method(g_friendsMethods[index(FriendsSlot::requestUserInformation)],
               &methods::request_user_information);
    set_method(g_friendsMethods[index(FriendsSlot::userRestrictions)],
               &methods::user_restrictions);
    set_method(g_friendsMethods[index(FriendsSlot::setRichPresence)],
               &methods::set_rich_presence);
    set_method(g_friendsMethods[index(FriendsSlot::clearRichPresence)],
               &methods::clear_rich_presence);
    set_method(g_friendsMethods[index(FriendsSlot::friendRichPresence)],
               &methods::friend_rich_presence);
    set_method(g_friendsMethods[index(FriendsSlot::friendRichPresenceKeyCount)],
               &methods::friend_rich_presence_key_count);
    set_method(g_friendsMethods[index(FriendsSlot::friendRichPresenceKeyByIndex)],
               &methods::friend_rich_presence_key_by_index);
    set_method(g_friendsMethods[index(FriendsSlot::requestFriendRichPresence)],
               &methods::request_friend_rich_presence);
    set_method(g_friendsMethods[index(FriendsSlot::inviteUserToGame)],
               &methods::invite_user_to_game);
    set_method(g_friendsMethods[index(FriendsSlot::listenForFriendsMessages)],
               &methods::set_listen_for_friends_messages);
    set_method(g_friendsMethods[index(FriendsSlot::replyToFriendMessage)],
               &methods::reply_to_friend_message);

    set_method(g_userMethods[index(UserSlot::handle)], &methods::get_user_handle);
    set_method(g_userMethods[index(UserSlot::loggedOn)], &methods::return_true);
    set_method(g_userMethods[index(UserSlot::steamId)], &methods::get_steam_id);
    set_method(g_userMethods[index(UserSlot::requestEncryptedAppTicket)],
               &methods::request_encrypted_app_ticket);
    set_method(g_userMethods[index(UserSlot::getEncryptedAppTicket)],
               &methods::get_encrypted_app_ticket);

    set_method(g_userStatsMethods[index(UserStatsSlot::requestCurrent)],
               &methods::request_current_stats);
    set_method(g_userStatsMethods[index(UserStatsSlot::achievement)], &methods::get_achievement);
    set_method(g_userStatsMethods[index(UserStatsSlot::setAchievement)], &methods::return_true);
    set_method(g_userStatsMethods[index(UserStatsSlot::store)], &methods::return_true);
}

void* apps() noexcept {
    return &g_apps;
}
void* input() noexcept {
    return &g_input;
}
void* utils() noexcept {
    return &g_utils;
}
void* friends() noexcept {
    return &g_friends;
}
void* user() noexcept {
    return &g_user;
}
void* user_stats() noexcept {
    return &g_userStats;
}

} // namespace dawn::steam::interfaces::tables
