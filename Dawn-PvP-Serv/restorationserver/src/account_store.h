#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "state/account/account_state.h"
#include "state/activity/definition.h"
#include "state/social/definition.h"
#include "state/runtime/runtime.h"
#include "state/runtime/state.h"

namespace restoration::account_store {

inline constexpr std::size_t kDiscordLinkCodeCapacity = 10;
inline constexpr std::size_t kAccountNameCapacity = 64;

struct DiscordLinkCode final {
    std::array<char, kDiscordLinkCodeCapacity> code{};
    std::array<char, kAccountNameCapacity> accountName{};
    std::uint64_t expiresAt{};
};

struct DiscordLinkState final {
    bool linked{};
    std::array<char, kAccountNameCapacity> accountName{};
};

struct PendingUnlink final {
    std::array<char, 21> discordUserId{};
    std::array<char, kAccountNameCapacity> accountName{};
};

enum class DiscordAction : std::uint8_t {
    friendAdd,
    friendAccept,
    friendDecline,
    friendRemove,
    friendList,
    accountRename,
};

enum class RelationshipAction : std::uint8_t {
    add = 1,
    accept = 2,
    decline = 3,
    remove = 4,
};

inline constexpr std::size_t kDirectMessageCapacity = 512;
inline constexpr std::size_t kDirectMessagePage = 32;

struct DirectMessage final {
    std::uint64_t id{};
    std::uint64_t sentAt{};
    std::uint64_t peerPlatformId{};
    bool outgoing{};
    std::array<char, kAccountNameCapacity> peerName{};
    std::array<char, kDirectMessageCapacity> body{};
};

struct LoadResult final {
    dawn::state::AccountState account{};
    dawn::state::activity::ActivityState activity{};
    dawn::state::social::Snapshot social{};
    std::uint64_t revision{};
    std::uint64_t activityRevision{};
    bool seeded{};
    bool activityRestored{};
};

struct DeviceBinding final {
    dawn::state::AccountRuntimeContext runtime{};
    std::uint64_t accountSoid{};
    std::uint64_t platformId{};
    bool created{};
};

[[nodiscard]] bool initialize(void* module,
                              const dawn::state::AccountState& seed,
                              std::string_view displayName,
                              std::uint64_t platformId,
                              LoadResult& result) noexcept;

void install_state_hooks() noexcept;

[[nodiscard]] bool save(const dawn::state::AccountState& account) noexcept;

struct WeaponAcquireRequest {
    std::uint64_t requestId{}, characterSoid{};
    std::uint32_t definitionHash{};
    std::array<std::uint32_t, dawn::state::account::inventory::kPlugCapacity> plugs{};
    std::size_t plugCount{};
};
struct WeaponAcquireResult {
    std::uint64_t characterSoid{}, instanceSoid{}, revision{};
    std::uint32_t definitionHash{};
};
[[nodiscard]] bool acquire_weapon(std::uint64_t platformId, const WeaponAcquireRequest& request,
                                  WeaponAcquireResult& result, std::string_view& error) noexcept;

[[nodiscard]] bool delete_character(std::uint64_t characterSoid) noexcept;

[[nodiscard]] bool create_character(std::uint8_t characterClass,
                                    std::uint8_t race,
                                    std::uint8_t gender) noexcept;

[[nodiscard]] bool create_character(const dawn::state::CharacterCreateRequest& request,
                                    std::uint64_t& createdSoid) noexcept;

[[nodiscard]] bool own_characters(std::uint64_t platformId,
                                  std::span<char> output,
                                  std::size_t& written) noexcept;

[[nodiscard]] bool mark_customize(std::uint64_t platformId, std::uint64_t characterSoid) noexcept;

[[nodiscard]] bool list_characters(std::span<char> output, std::size_t& written) noexcept;

[[nodiscard]] bool save_callback(void* context,
                                 const dawn::state::AccountState& account) noexcept;

[[nodiscard]] bool save_activity(
    const dawn::state::activity::ActivityState& activity) noexcept;

[[nodiscard]] bool save_activity_callback(
    void* context,
    const dawn::state::activity::ActivityState& activity) noexcept;

[[nodiscard]] bool issue_discord_link_code(std::uint64_t platformId,
                                           std::uint64_t nowSeconds,
                                           DiscordLinkCode& result) noexcept;

[[nodiscard]] bool redeem_discord_link(std::string_view discordUserId,
                                       std::string_view code,
                                       std::uint64_t nowSeconds,
                                       std::span<char> message,
                                       std::size_t& written) noexcept;

[[nodiscard]] bool discord_link_state(std::uint64_t platformId,
                                      DiscordLinkState& output) noexcept;

[[nodiscard]] bool request_discord_unlink(std::uint64_t platformId,
                                          std::uint64_t nowSeconds,
                                          DiscordLinkState& output) noexcept;

[[nodiscard]] bool claim_discord_unlinks(std::span<PendingUnlink> output,
                                         std::size_t& count) noexcept;

[[nodiscard]] bool execute_discord_unlink(std::string_view discordUserId,
                                          bool confirmed,
                                          std::uint64_t nowSeconds,
                                          std::span<char> message,
                                          std::size_t& written) noexcept;

[[nodiscard]] bool rename_account(std::uint64_t platformId,
                                  std::string_view name,
                                  std::uint64_t nowSeconds,
                                  std::span<char> message,
                                  std::size_t& written) noexcept;

[[nodiscard]] bool execute_discord_action(std::string_view discordUserId,
                                          DiscordAction action,
                                          std::string_view argument,
                                          std::uint64_t nowSeconds,
                                          std::span<char> message,
                                          std::size_t& written) noexcept;

// the actor is resolved from platformId, never taken from the request
[[nodiscard]] bool execute_relationship(std::uint64_t platformId,
                                        RelationshipAction action,
                                        std::string_view targetName,
                                        std::uint64_t nowSeconds,
                                        std::span<char> message,
                                        std::size_t& written) noexcept;

[[nodiscard]] bool send_direct_message(std::uint64_t platformId,
                                       std::string_view targetName,
                                       std::string_view body,
                                       std::uint64_t nowSeconds,
                                       std::span<char> message,
                                       std::size_t& written) noexcept;

inline constexpr std::size_t kAvatarCapacity = 64;

struct FriendAvatar final {
    std::uint64_t platformId{};
    std::array<char, kAvatarCapacity> avatar{};
};

[[nodiscard]] bool exchange_avatars(std::uint64_t platformId,
                                    std::string_view avatar,
                                    std::uint64_t nowSeconds,
                                    std::span<FriendAvatar> output,
                                    std::size_t& count) noexcept;

[[nodiscard]] bool poll_direct_messages(std::uint64_t platformId,
                                        std::uint64_t cursor,
                                        std::span<DirectMessage> output,
                                        std::size_t& count,
                                        std::uint64_t& nextCursor) noexcept;

[[nodiscard]] bool social_snapshot(std::uint64_t platformId,
                                   dawn::state::social::Snapshot& output) noexcept;

[[nodiscard]] bool live_friend(std::uint64_t viewerAccountSoid,
                               std::uint64_t targetAccountSoid,
                               dawn::state::social::RemoteAccount& output) noexcept;

[[nodiscard]] bool accepted_friend(std::uint64_t leftPlatformId,
                                   std::uint64_t rightPlatformId) noexcept;

[[nodiscard]] bool friend_profile(std::uint64_t viewerPlatformId,
                                  std::uint64_t targetPlatformId,
                                  dawn::state::AccountState& output,
                                  std::uint64_t& revision) noexcept;

[[nodiscard]] bool profile_by_platform(std::uint64_t platformId,
                                       dawn::state::AccountState& output,
                                       std::uint64_t& revision,
                                       std::array<char, kAccountNameCapacity>& displayName) noexcept;

// bridges activity (account soid) and gameplay membership (platform id)
[[nodiscard]] bool profile_by_account(
    std::uint64_t accountSoid,
    std::uint64_t& platformId,
    dawn::state::AccountState& output,
    std::uint64_t& revision,
    std::array<char, kAccountNameCapacity>& displayName) noexcept;

[[nodiscard]] bool presentation_by_account(std::uint64_t accountSoid,
    std::uint64_t& platformId, std::array<char, kAccountNameCapacity>& displayName) noexcept;

[[nodiscard]] bool load_or_create_device(std::uint64_t deviceId,
                                         std::uint64_t platformId,
                                         std::span<const std::byte> publicKey,
                                         DeviceBinding& output) noexcept;

[[nodiscard]] bool resolve_runtime_callback(void* context,
                                            std::uint64_t accountSoid,
                                            std::uint64_t platformId,
                                            dawn::state::AccountRuntimeContext& output) noexcept;

[[nodiscard]] bool resolve_friend_account_callback(void* context,
                                                   std::uint64_t viewerPlatformId,
                                                   std::uint64_t targetPlatformId,
                                                   std::uint64_t& targetAccountSoid) noexcept;

[[nodiscard]] bool resolve_friend_profile_callback(void* context,
                                                   std::uint64_t viewerAccountSoid,
                                                   std::uint64_t targetAccountSoid,
                                                   dawn::state::AccountState& output) noexcept;

enum class ExpansionRecord : std::uint8_t {
    ok,
    notLinked,
    steamMismatch,
    steamInUse,
    failed,
};

struct ExpansionOwnership final {
    bool known{};
    bool forsaken{};
    bool shadowkeep{};
};

[[nodiscard]] ExpansionOwnership expansion_ownership(std::uint64_t platformId) noexcept;

[[nodiscard]] ExpansionRecord record_expansion_check(std::string_view discordUserId,
                                                     std::uint64_t steamId,
                                                     std::string_view steamAccountName,
                                                     bool forsaken,
                                                     bool shadowkeep,
                                                     std::uint64_t nowSeconds,
                                                     std::uint64_t& pinnedSteamId) noexcept;

[[nodiscard]] bool set_discord_roles(std::string_view discordUserId,
                                     std::uint32_t roleMask,
                                     std::uint64_t nowSeconds) noexcept;

[[nodiscard]] bool prune_discord_roles(std::uint64_t syncStartedAt) noexcept;

[[nodiscard]] std::uint32_t discord_role_mask(std::string_view discordUserId) noexcept;

[[nodiscard]] std::uint32_t discord_access_grants(std::string_view discordUserId) noexcept;

[[nodiscard]] bool discord_linked_account(std::string_view discordUserId,
                                          std::array<char, kAccountNameCapacity>& accountName) noexcept;

[[nodiscard]] bool create_access_code(std::span<const std::byte> codeHash,
                                      std::uint32_t grants,
                                      std::string_view createdBy,
                                      std::uint64_t nowSeconds) noexcept;

enum class AccessCodeRedeem : std::uint8_t {
    ok,
    notFound,
    alreadyUsed,
    failed,
};

[[nodiscard]] AccessCodeRedeem redeem_access_code(std::span<const std::byte> codeHash,
                                                  std::string_view discordUserId,
                                                  std::uint64_t nowSeconds,
                                                  std::uint32_t& grants) noexcept;

[[nodiscard]] bool clear_discord_access_grants(std::string_view discordUserId) noexcept;

void shutdown() noexcept;

} // namespace restoration::account_store
