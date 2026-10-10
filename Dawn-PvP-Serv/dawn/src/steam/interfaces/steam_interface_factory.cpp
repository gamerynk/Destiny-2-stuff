#include "steam_interface_factory.h"

#include <array>
#include <cstring>

#include "internal.h"

namespace dawn::steam::interfaces {
namespace {

struct UserInterfaceEntry {
    const char* version{};
    void* (*get)() noexcept {};
};

constexpr std::array kUserInterfaces{
    UserInterfaceEntry{versions::kApps, &tables::apps},
    UserInterfaceEntry{versions::kInput, &tables::input},
    UserInterfaceEntry{versions::kUtils, &tables::utils},
    UserInterfaceEntry{versions::kFriends, &tables::friends},
    UserInterfaceEntry{versions::kUser, &tables::user},
    UserInterfaceEntry{versions::kUserStats, &tables::user_stats},
    UserInterfaceEntry{versions::kMatchmaking, &tables::matchmaking},
};

} // namespace

void* create(const char* version) noexcept {
    if (version == nullptr || !tables::initialize()) {
        return nullptr;
    }
    return std::strcmp(version, versions::kLegacyClient) == 0 ? tables::client() : nullptr;
}

void* find_user(const char* version) noexcept {
    if (version == nullptr || !tables::initialize()) {
        return nullptr;
    }
    for (const auto& entry : kUserInterfaces) {
        if (std::strcmp(version, entry.version) == 0) {
            return entry.get();
        }
    }
    return nullptr;
}

void* find_global(const char* version) noexcept {
    if (version == nullptr || !tables::initialize()) {
        return nullptr;
    }
    return std::strcmp(version, versions::kUtils) == 0 ? tables::utils() : nullptr;
}

} // namespace dawn::steam::interfaces
