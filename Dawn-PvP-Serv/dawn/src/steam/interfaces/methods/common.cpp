#include <cstdint>
#include <cstring>

#include "../../../core/settings/settings.h"
#include "../../runtime/transport/steam_transport_bridge.h"
#include "../internal.h"
#include "../steam_interface_factory.h"

namespace dawn::steam::interfaces::methods {
namespace {

constexpr int kUserStatsReceivedCallback = 1101;
constexpr int kResultOk = 1;
constexpr int kConnectedUniverse = 1;
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr std::size_t kUserStatsReceivedSize = 24;

struct UserStatsReceived {
    std::uint64_t gameId{};
    int result{};
    DWORD padding{};
    std::uint64_t userId{};
};

static_assert(sizeof(UserStatsReceived) == kUserStatsReceivedSize);

[[nodiscard]] std::uint64_t hash_name(const char* value) noexcept {
    if (value == nullptr || *value == '\0') {
        return 0;
    }
    std::uint64_t hash = kFnvOffsetBasis;
    while (*value != '\0') {
        hash ^= static_cast<unsigned char>(*value++);
        hash *= kFnvPrime;
    }
    return hash == 0 ? 1 : hash;
}

} // namespace

ULONG_PTR empty([[maybe_unused]] void* self) noexcept {
    return 0;
}

bool return_true([[maybe_unused]] void* self) noexcept {
    return true;
}

const char* language([[maybe_unused]] void* self) noexcept {
    return core::settings::get().steam.language.data();
}

UserHandle get_user_handle([[maybe_unused]] void* self) noexcept {
    return user_handle();
}

SteamId* get_steam_id([[maybe_unused]] void* self, SteamId* result) noexcept {
    if (result != nullptr) {
        result->value = local_steam_id();
    }
    return result;
}

SteamId* get_app_owner([[maybe_unused]] void* self, SteamId* result) noexcept {
    if (result != nullptr) {
        result->value = local_steam_id();
    }
    return result;
}

// a DLC id reported owned but not installed makes the client treat its content as missing
bool app_is_installed([[maybe_unused]] void* self, [[maybe_unused]] DWORD candidate) noexcept {
    return true;
}

bool request_current_stats([[maybe_unused]] void* self) noexcept {
    const UserStatsReceived received{app_id(), kResultOk, 0, 0};
    return queue_callback(kUserStatsReceivedCallback, 0, &received, sizeof(received));
}

bool get_achievement([[maybe_unused]] void* self,
                     [[maybe_unused]] const char* name,
                     bool* achieved) noexcept {
    if (achieved == nullptr) {
        return false;
    }
    *achieved = false;
    return true;
}

std::uint64_t input_handle([[maybe_unused]] void* self, const char* name) noexcept {
    return hash_name(name);
}

InputDigitalActionData input_digital_data([[maybe_unused]] void* self,
                                          [[maybe_unused]] std::uint64_t controllerHandle,
                                          [[maybe_unused]] std::uint64_t actionHandle) noexcept {
    return {};
}

InputAnalogActionData input_analog_data([[maybe_unused]] void* self,
                                        [[maybe_unused]] std::uint64_t controllerHandle,
                                        [[maybe_unused]] std::uint64_t actionHandle) noexcept {
    return {};
}

InputMotionData input_motion_data([[maybe_unused]] void* self,
                                  [[maybe_unused]] std::uint64_t controllerHandle) noexcept {
    return {};
}

int filter_text([[maybe_unused]] void* self,
                char* output,
                DWORD outputSize,
                const char* input,
                [[maybe_unused]] bool compatibilityFlag) noexcept {
    if (output == nullptr || outputSize == 0) {
        return 0;
    }
    if (input == nullptr) {
        output[0] = '\0';
        return 0;
    }
    DWORD count{};
    while (count + 1 < outputSize && input[count] != '\0') {
        output[count] = input[count];
        ++count;
    }
    output[count] = '\0';
    return static_cast<int>(count);
}

PipeHandle create_steam_pipe([[maybe_unused]] void* self) noexcept {
    return pipe_handle();
}

// release is accepted without invalidating the pipe: SteamClient consumers in this
// process share it
bool release_steam_pipe([[maybe_unused]] void* self, PipeHandle pipe) noexcept {
    return pipe == pipe_handle();
}

UserHandle connect_to_global_user([[maybe_unused]] void* self, PipeHandle pipe) noexcept {
    return pipe == pipe_handle() ? user_handle() : 0;
}

PipeHandle create_local_user([[maybe_unused]] void* self,
                             UserHandle* user,
                             [[maybe_unused]] int accountType) noexcept {
    if (user == nullptr) {
        return 0;
    }
    *user = user_handle();
    return pipe_handle();
}

void release_user([[maybe_unused]] void* self,
                  [[maybe_unused]] PipeHandle pipe,
                  [[maybe_unused]] UserHandle user) noexcept {}

void* get_generic_interface([[maybe_unused]] void* self,
                            UserHandle user,
                            PipeHandle pipe,
                            const char* version) noexcept {
    if (user != user_handle() || pipe != pipe_handle() || version == nullptr) {
        return nullptr;
    }
    if (std::strcmp(version, versions::kSerializedNetworking) == 0) {
        return tables::serialized_networking();
    }
    if (std::strcmp(version, versions::kHttp) == 0) {
        return runtime::transport::http_interface();
    }
    return ::dawn::steam::interfaces::find_user(version);
}

void* get_client_user([[maybe_unused]] void* self,
                      UserHandle user,
                      PipeHandle pipe,
                      const char* version) noexcept {
    return user == user_handle() && pipe == pipe_handle() && version != nullptr
                   && std::strcmp(version, versions::kUser) == 0
               ? tables::user()
               : nullptr;
}

void* get_client_friends([[maybe_unused]] void* self,
                         UserHandle user,
                         PipeHandle pipe,
                         const char* version) noexcept {
    return user == user_handle() && pipe == pipe_handle() && version != nullptr
                   && std::strcmp(version, versions::kFriends) == 0
               ? tables::friends()
               : nullptr;
}

void* get_client_matchmaking([[maybe_unused]] void* self,
                             UserHandle user,
                             PipeHandle pipe,
                             const char* version) noexcept {
    return user == user_handle() && pipe == pipe_handle() && version != nullptr
                   && std::strcmp(version, versions::kMatchmaking) == 0
               ? tables::matchmaking()
               : nullptr;
}

void* get_client_user_stats([[maybe_unused]] void* self,
                            UserHandle user,
                            PipeHandle pipe,
                            const char* version) noexcept {
    return user == user_handle() && pipe == pipe_handle() && version != nullptr
                   && std::strcmp(version, versions::kUserStats) == 0
               ? tables::user_stats()
               : nullptr;
}

void* get_client_apps([[maybe_unused]] void* self,
                      UserHandle user,
                      PipeHandle pipe,
                      const char* version) noexcept {
    return user == user_handle() && pipe == pipe_handle() && version != nullptr
                   && std::strcmp(version, versions::kApps) == 0
               ? tables::apps()
               : nullptr;
}

void* get_client_input([[maybe_unused]] void* self,
                       UserHandle user,
                       PipeHandle pipe,
                       const char* version) noexcept {
    return user == user_handle() && pipe == pipe_handle() && version != nullptr
                   && std::strcmp(version, versions::kInput) == 0
               ? tables::input()
               : nullptr;
}

void* get_client_utils([[maybe_unused]] void* self, PipeHandle pipe, const char* version) noexcept {
    return pipe == pipe_handle() && version != nullptr
                   && std::strcmp(version, versions::kUtils) == 0
               ? tables::utils()
               : nullptr;
}

void* get_client_http([[maybe_unused]] void* self,
                      UserHandle user,
                      PipeHandle pipe,
                      const char* version) noexcept {
    return user == user_handle() && pipe == pipe_handle() && version != nullptr
               && std::strcmp(version, versions::kHttp) == 0
               ? runtime::transport::http_interface()
               : nullptr;
}

int connected_universe([[maybe_unused]] void* self) noexcept {
    return kConnectedUniverse;
}

} // namespace dawn::steam::interfaces::methods
