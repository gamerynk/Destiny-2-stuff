#include "account_store.h"

#include <Windows.h>
#include <bcrypt.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <string>
#include <vector>

#include "account_codec.h"
#include "state/build_data/runtime.h"
#include "middleware/datagen/family4/loadout/loadout_resolver.h"
#include "activity_codec.h"
#include "core/filesystem/path.h"
#include "core/identity/device_identity.h"
#include "core/logging/log.h"
#include "core/settings/rule_text.h"
#include "core/settings/settings.h"
#include "realtime_store.h"
#include "server/bap/runtime.h"
#include "state/account/inventory/inventory_state.h"
#include "state/account/inventory/weapon_roll_policy.h"
#include "state/build_data/items/item_catalog.h"
#include "state/runtime/runtime.h"
#include "state/equipment/light/resolution/configured_equipment_light_resolver.h"
#include "state/runtime/runtime.h"

namespace restoration::account_store {
namespace {

constexpr std::wstring_view kStorageDirectorySuffix = L"\\storage";
constexpr std::wstring_view kDatabaseFileSuffix = L"\\restoration.db";
constexpr std::string_view kProvider = "restoration-steam";
constexpr std::string_view kDefaultLocation = "In Orbit";
constexpr std::uint64_t kAccountSoidBase = 0x9EAA000100000100ULL;
constexpr std::uint32_t kMaximumAccountNamespace = 4095;
constexpr std::uint32_t kDestinyAppId = 1085660;
constexpr int kBusyTimeoutMilliseconds = 5000;
constexpr std::size_t kDigestSize = 32;
constexpr std::uint64_t kDiscordLinkLifetimeSeconds = 5 * 60;
constexpr std::uint64_t kDiscordUnlinkLifetimeSeconds = 10 * 60;
constexpr std::size_t kCanonicalLinkCodeSize = 8;
constexpr std::string_view kLinkAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

sqlite3* g_database{};
sqlite3_int64 g_accountId{};
std::uint64_t g_revision{};
std::uint64_t g_activityRevision{};
SRWLOCK g_lock{SRWLOCK_INIT};

struct RuntimeRecord final {
    dawn::state::AccountState account{};
    dawn::state::activity::ActivityState activity{};
    sqlite3_int64 accountId{};
    std::uint64_t platformId{};
    std::uint64_t revision{};
    std::uint64_t activityRevision{};
};

std::vector<std::unique_ptr<RuntimeRecord>> g_runtimeRecords;
dawn::state::AccountState g_starterSeed{};

void report(const char* stage, const char* result, std::uint64_t revision = 0) noexcept {
    std::array<char, dawn::core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=account_store stage=%s result=%s revision=%llu",
                                      stage,
                                      result,
                                      static_cast<unsigned long long>(revision));
    if (written > 0) {
        dawn::core::log::write(dawn::core::log::Channel::state,
                                  std::strcmp(result, "ok") == 0
                                      ? dawn::core::log::Level::info
                                      : dawn::core::log::Level::warn,
                                  {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] bool execute(sqlite3* database, const char* sql) noexcept {
    return database != nullptr && sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) noexcept {
        if (database != nullptr) {
            (void)sqlite3_prepare_v2(database, sql, -1, &value_, nullptr);
        }
    }

    ~Statement() {
        if (value_ != nullptr) {
            (void)sqlite3_finalize(value_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return value_;
    }

private:
    sqlite3_stmt* value_{};
};

[[nodiscard]] bool sha256(std::span<const std::byte> input,
                          std::array<std::byte, kDigestSize>& output) noexcept {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    bool success = BCryptOpenAlgorithmProvider(
                       &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)
                       >= 0
                   && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0
                   && BCryptHashData(hash,
                                     reinterpret_cast<PUCHAR>(const_cast<std::byte*>(input.data())),
                                     static_cast<ULONG>(input.size()),
                                     0)
                          >= 0
                   && BCryptFinishHash(hash,
                                       reinterpret_cast<PUCHAR>(output.data()),
                                       static_cast<ULONG>(output.size()),
                                       0)
                          >= 0;
    if (hash != nullptr) {
        (void)BCryptDestroyHash(hash);
    }
    if (algorithm != nullptr) {
        (void)BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    if (!success) {
        SecureZeroMemory(output.data(), output.size());
    }
    return success;
}

[[nodiscard]] bool bind_u64_blob(sqlite3_stmt* statement,
                                 int index,
                                 const std::uint64_t& value) noexcept {
    return statement != nullptr
           && sqlite3_bind_blob(statement,
                                index,
                                &value,
                                static_cast<int>(sizeof value),
                                SQLITE_TRANSIENT)
                  == SQLITE_OK;
}

[[nodiscard]] bool bind_bytes(sqlite3_stmt* statement,
                              int index,
                              std::span<const std::byte> bytes) noexcept {
    return statement != nullptr
           && bytes.size() <= static_cast<std::size_t>((std::numeric_limits<int>::max)())
           && sqlite3_bind_blob(statement,
                                index,
                                bytes.data(),
                                static_cast<int>(bytes.size()),
                                SQLITE_TRANSIENT)
                  == SQLITE_OK;
}

[[nodiscard]] bool bind_text(sqlite3_stmt* statement,
                             int index,
                             std::string_view text) noexcept {
    return statement != nullptr
           && text.size() <= static_cast<std::size_t>((std::numeric_limits<int>::max)())
           && sqlite3_bind_text(statement,
                                index,
                                text.data(),
                                static_cast<int>(text.size()),
                                SQLITE_TRANSIENT)
                  == SQLITE_OK;
}

[[nodiscard]] bool make_database_path(void* module, dawn::core::path::Buffer& path) noexcept {
    if (!dawn::core::path::artifact_directory(module, path)
        || !dawn::core::path::append(path, kStorageDirectorySuffix)) {
        return false;
    }
    if (CreateDirectoryW(path.chars.data(), nullptr) == FALSE) {
        const DWORD error = GetLastError();
        const DWORD attributes = GetFileAttributesW(path.chars.data());
        if (error != ERROR_ALREADY_EXISTS || attributes == INVALID_FILE_ATTRIBUTES
            || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return false;
        }
    }
    return dawn::core::path::append(path, kDatabaseFileSuffix);
}

[[nodiscard]] bool migrate(sqlite3* database) noexcept {
    constexpr const char* schema =
        "BEGIN IMMEDIATE;"
        "CREATE TABLE IF NOT EXISTS schema_migrations("
        " version INTEGER PRIMARY KEY NOT NULL,"
        " applied_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS accounts("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " account_soid BLOB NOT NULL UNIQUE CHECK(length(account_soid)=8),"
        " display_name TEXT NOT NULL UNIQUE,"
        " revision INTEGER NOT NULL CHECK(revision>=1),"
        " state_codec INTEGER NOT NULL,"
        " state_blob BLOB NOT NULL,"
        " state_hash BLOB NOT NULL CHECK(length(state_hash)=32),"
        " created_at INTEGER NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS platform_identities("
        " provider TEXT NOT NULL,"
        " external_id BLOB NOT NULL CHECK(length(external_id)=8),"
        " account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " created_at INTEGER NOT NULL,"
        " PRIMARY KEY(provider, external_id),"
        " UNIQUE(provider, account_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS activity_checkpoints("
        " account_id INTEGER PRIMARY KEY NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " revision INTEGER NOT NULL CHECK(revision>=1),"
        " state_codec INTEGER NOT NULL,"
        " state_blob BLOB NOT NULL,"
        " state_hash BLOB NOT NULL CHECK(length(state_hash)=32),"
        " created_at INTEGER NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS friendships("
        " requester_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " recipient_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " state INTEGER NOT NULL CHECK(state BETWEEN 1 AND 3),"
        " created_at INTEGER NOT NULL,"
        " updated_at INTEGER NOT NULL,"
        " PRIMARY KEY(requester_account_id,recipient_account_id),"
        " CHECK(requester_account_id<>recipient_account_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS discord_links("
        " discord_user_id TEXT PRIMARY KEY NOT NULL,"
        " account_id INTEGER NOT NULL UNIQUE REFERENCES accounts(id) ON DELETE CASCADE,"
        " linked_at INTEGER NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS discord_link_codes("
        " account_id INTEGER PRIMARY KEY NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " code_hash BLOB NOT NULL CHECK(length(code_hash)=32),"
        " expires_at INTEGER NOT NULL,"
        " created_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS discord_unlink_requests("
        " account_id INTEGER PRIMARY KEY NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " discord_user_id TEXT NOT NULL,"
        " created_at INTEGER NOT NULL,"
        " expires_at INTEGER NOT NULL,"
        " notified_at INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS weapon_acquire_receipts("
        " account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " request_id BLOB NOT NULL CHECK(length(request_id)=8),"
        " request_text TEXT NOT NULL, instance_soid BLOB NOT NULL CHECK(length(instance_soid)=8),"
        " character_soid BLOB NOT NULL CHECK(length(character_soid)=8),"
        " definition_hash INTEGER NOT NULL, revision INTEGER NOT NULL,"
        " PRIMARY KEY(account_id,request_id));"
        "CREATE TABLE IF NOT EXISTS account_namespaces("
        " account_id INTEGER PRIMARY KEY NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " namespace INTEGER NOT NULL UNIQUE CHECK(namespace BETWEEN 1 AND 4095)"
        ");"
        "CREATE TABLE IF NOT EXISTS device_credentials("
        " platform_id BLOB PRIMARY KEY NOT NULL CHECK(length(platform_id)=8),"
        " public_key BLOB NOT NULL UNIQUE CHECK(length(public_key)=72),"
        " account_id INTEGER NOT NULL UNIQUE REFERENCES accounts(id) ON DELETE CASCADE,"
        " created_at INTEGER NOT NULL,"
        " last_seen INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS steam_expansion_pins("
        " account_id INTEGER PRIMARY KEY NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " steam_id BLOB NOT NULL UNIQUE CHECK(length(steam_id)=8),"
        " steam_account_name TEXT NOT NULL,"
        " forsaken INTEGER NOT NULL CHECK(forsaken IN (0,1)),"
        " shadowkeep INTEGER NOT NULL CHECK(shadowkeep IN (0,1)),"
        " pinned_at INTEGER NOT NULL,"
        " checked_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS discord_member_roles("
        " discord_user_id TEXT PRIMARY KEY NOT NULL,"
        " role_mask INTEGER NOT NULL CHECK(role_mask>0),"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS access_codes("
        " code_hash BLOB PRIMARY KEY NOT NULL CHECK(length(code_hash)=32),"
        " grants INTEGER NOT NULL CHECK(grants>0),"
        " created_by TEXT NOT NULL,"
        " created_at INTEGER NOT NULL,"
        " redeemed_by TEXT,"
        " redeemed_at INTEGER"
        ");"
        "CREATE TABLE IF NOT EXISTS discord_access_grants("
        " discord_user_id TEXT PRIMARY KEY NOT NULL,"
        " grants INTEGER NOT NULL CHECK(grants>0),"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS account_avatars("
        " account_id INTEGER PRIMARY KEY NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " avatar TEXT NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS direct_messages("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " from_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " to_account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,"
        " body TEXT NOT NULL,"
        " sent_at INTEGER NOT NULL,"
        " CHECK(from_account_id<>to_account_id)"
        ");"
        "CREATE INDEX IF NOT EXISTS direct_messages_to ON direct_messages(to_account_id,id);"
        "CREATE INDEX IF NOT EXISTS direct_messages_from ON direct_messages(from_account_id,id);"
        "CREATE UNIQUE INDEX IF NOT EXISTS accounts_display_name_nocase"
        " ON accounts(display_name COLLATE NOCASE);"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(1,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(2,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(3,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(4,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(5,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(6,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(7,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(8,CAST(strftime('%s','now') AS INTEGER));"
        "INSERT OR IGNORE INTO schema_migrations(version,applied_at)"
        " VALUES(9,CAST(strftime('%s','now') AS INTEGER));"
        "PRAGMA user_version=9;"
        "COMMIT;";
    return execute(database, "PRAGMA foreign_keys=ON;")
           && execute(database, "PRAGMA journal_mode=WAL;")
           && execute(database, "PRAGMA synchronous=FULL;")
           && execute(database, schema);
}

[[nodiscard]] bool seed_account(sqlite3* database,
                                const dawn::state::AccountState& seed,
                                std::string_view displayName,
                                std::uint64_t platformId,
                                sqlite3_int64& accountId) noexcept {
    std::vector<std::byte> encoded;
    std::array<std::byte, kDigestSize> digest{};
    if (!account_codec::self_test(seed) || !account_codec::encode(seed, encoded)
        || !sha256(encoded, digest) || !execute(database, "BEGIN IMMEDIATE;")) {
        return false;
    }
    Statement accountStatement(
        database,
        "INSERT INTO accounts(account_soid,display_name,revision,state_codec,state_blob,"
        "state_hash,created_at,updated_at) VALUES(?1,?2,1,?3,?4,?5,"
        "CAST(strftime('%s','now') AS INTEGER),CAST(strftime('%s','now') AS INTEGER));");
    sqlite3_stmt* account = accountStatement.get();
    bool success = bind_u64_blob(account, 1, seed.primarySoid) && bind_text(account, 2, displayName)
                   && sqlite3_bind_int(account, 3, account_codec::kVersion) == SQLITE_OK
                   && bind_bytes(account, 4, encoded) && bind_bytes(account, 5, digest)
                   && sqlite3_step(account) == SQLITE_DONE;
    if (success) {
        accountId = sqlite3_last_insert_rowid(database);
        Statement identityStatement(
            database,
            "INSERT INTO platform_identities(provider,external_id,account_id,created_at)"
            " VALUES(?1,?2,?3,CAST(strftime('%s','now') AS INTEGER));");
        sqlite3_stmt* identity = identityStatement.get();
        success = bind_text(identity, 1, kProvider) && bind_u64_blob(identity, 2, platformId)
                  && sqlite3_bind_int64(identity, 3, accountId) == SQLITE_OK
                  && sqlite3_step(identity) == SQLITE_DONE;
    }
    success = success && execute(database, "COMMIT;");
    if (!success) {
        (void)execute(database, "ROLLBACK;");
    }
    SecureZeroMemory(digest.data(), digest.size());
    return success;
}

enum class LoadStatus : std::uint8_t {
    missing,
    loaded,
    failed,
};

[[nodiscard]] LoadStatus load_activity(
    sqlite3* database,
    sqlite3_int64 accountId,
    std::uint64_t ownerAccountSoid,
    std::uint64_t& revision,
    dawn::state::activity::ActivityState& output) noexcept {
    Statement statement(
        database,
        "SELECT revision,state_codec,state_blob,state_hash FROM activity_checkpoints "
        "WHERE account_id=?1;");
    sqlite3_stmt* query = statement.get();
    if (query == nullptr || sqlite3_bind_int64(query, 1, accountId) != SQLITE_OK) {
        return LoadStatus::failed;
    }
    const int step = sqlite3_step(query);
    if (step == SQLITE_DONE) {
        return LoadStatus::missing;
    }
    if (step != SQLITE_ROW || sqlite3_column_int(query, 1) != activity_codec::kVersion) {
        return LoadStatus::failed;
    }
    const sqlite3_int64 loadedRevision = sqlite3_column_int64(query, 0);
    const int blobSize = sqlite3_column_bytes(query, 2);
    const int hashSize = sqlite3_column_bytes(query, 3);
    const auto* blob = static_cast<const std::byte*>(sqlite3_column_blob(query, 2));
    const auto* storedHash = static_cast<const std::byte*>(sqlite3_column_blob(query, 3));
    if (loadedRevision < 1 || blobSize <= 0 || blob == nullptr
        || hashSize != static_cast<int>(kDigestSize) || storedHash == nullptr) {
        return LoadStatus::failed;
    }
    const std::span<const std::byte> payload(blob, static_cast<std::size_t>(blobSize));
    std::array<std::byte, kDigestSize> digest{};
    bool sameHash = sha256(payload, digest);
    unsigned difference = 0;
    for (std::size_t index = 0; index < digest.size() && sameHash; ++index) {
        difference |= std::to_integer<unsigned>(digest[index] ^ storedHash[index]);
    }
    dawn::state::activity::ActivityState decoded{};
    const bool decodedOk = sameHash && difference == 0
                           && activity_codec::decode(payload, ownerAccountSoid, decoded)
                           && activity_codec::self_test(decoded, ownerAccountSoid);
    SecureZeroMemory(digest.data(), digest.size());
    if (!decodedOk) {
        return LoadStatus::failed;
    }
    revision = static_cast<std::uint64_t>(loadedRevision);
    output = decoded;
    return LoadStatus::loaded;
}

[[nodiscard]] std::uint32_t character_item_count(
    const dawn::state::CharacterState& character) noexcept {
    std::uint32_t count = static_cast<std::uint32_t>(character.inventory.count);
    for (const auto& item : character.equipment.slots) {
        count += item.has_value() ? 1U : 0U;
    }
    return count;
}

void seed_character_serials(dawn::state::CharacterState& character) noexcept {
    std::uint32_t next = 0;
    for (auto& item : character.equipment.slots) {
        if (item.has_value()) {
            item->mutationSerial = static_cast<std::int32_t>(next++);
        }
    }
    for (std::size_t index = 0; index < character.inventory.count; ++index) {
        character.inventory.values[index].mutationSerial = static_cast<std::int32_t>(next++);
    }
    character.nextInventorySerial = next;
}

[[nodiscard]] bool grant_class_subclasses(dawn::state::AccountState& account) noexcept;
[[nodiscard]] std::size_t grant_listed_items(dawn::state::AccountState& account) noexcept;

// no character is selected at boot: a persisted pick makes the first login encode a character
// the client never picked; unseeded serials are re-seeded because the family-4 encoder rejects them
void prepare_loaded_account(dawn::state::AccountState& account) noexcept {
    const std::size_t count = (std::min)(account.characterCount, account.characters.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto& character = account.characters[index];
        character.selected = false;
        if (character.nextInventorySerial < character_item_count(character)) {
            seed_character_serials(character);
        }
    }
    if (grant_class_subclasses(account)) {
        report("subclass_grant", "ok");
    }
    if (grant_listed_items(account) != 0) {
        report("item_grant", "ok");
    }
}

[[nodiscard]] bool known_account_codec(int version) noexcept {
    return version == account_codec::kVersion || version == account_codec::kPreSetupVersion
           || version == account_codec::kPreFovVersion
           || version == account_codec::kPreInputVersion
           || version == account_codec::kVaultlessVersion
           || version == account_codec::kLegacyHeaderVersion
           || version == account_codec::kPriorVersion;
}

void report_account_refused(const char* loader, int codecVersion, const char* reason) noexcept {
    std::array<char, dawn::core::log::kLineCapacity> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=account_store stage=%s result=fail reason=%s codec=%d",
                                      loader,
                                      reason,
                                      codecVersion);
    if (written > 0) {
        dawn::core::log::write(dawn::core::log::Channel::state,
                                  dawn::core::log::Level::warn,
                                  {line.data(), static_cast<std::size_t>(written)});
    }
}

[[nodiscard]] LoadStatus load_account(sqlite3* database,
                                      std::uint64_t platformId,
                                      sqlite3_int64& accountId,
                                      std::uint64_t& revision,
                                      dawn::state::AccountState& output) noexcept {
    Statement statement(
        database,
        "SELECT a.id,a.revision,a.state_codec,a.state_blob,a.state_hash"
        " FROM accounts a JOIN platform_identities p ON p.account_id=a.id"
        " WHERE p.provider=?1 AND p.external_id=?2;");
    sqlite3_stmt* query = statement.get();
    if (!bind_text(query, 1, kProvider) || !bind_u64_blob(query, 2, platformId)) {
        return LoadStatus::failed;
    }
    const int step = sqlite3_step(query);
    if (step == SQLITE_DONE) {
        return LoadStatus::missing;
    }
    const int codecVersion = step == SQLITE_ROW ? sqlite3_column_int(query, 2) : 0;
    if (step != SQLITE_ROW || !known_account_codec(codecVersion)) {
        if (step == SQLITE_ROW) {
            report_account_refused("load_account", codecVersion, "codec");
        }
        return LoadStatus::failed;
    }
    const sqlite3_int64 loadedRevision = sqlite3_column_int64(query, 1);
    const int blobSize = sqlite3_column_bytes(query, 3);
    const int hashSize = sqlite3_column_bytes(query, 4);
    const auto* blob = static_cast<const std::byte*>(sqlite3_column_blob(query, 3));
    const auto* storedHash = static_cast<const std::byte*>(sqlite3_column_blob(query, 4));
    if (loadedRevision < 1 || blobSize <= 0 || blob == nullptr
        || hashSize != static_cast<int>(kDigestSize) || storedHash == nullptr) {
        return LoadStatus::failed;
    }
    const std::span<const std::byte> payload(blob, static_cast<std::size_t>(blobSize));
    std::array<std::byte, kDigestSize> digest{};
    bool sameHash = sha256(payload, digest);
    unsigned difference = 0;
    for (std::size_t index = 0; index < digest.size() && sameHash; ++index) {
        difference |= std::to_integer<unsigned>(digest[index] ^ storedHash[index]);
    }
    sameHash = sameHash && difference == 0;
    dawn::state::AccountState decoded{};
    const bool decodedOk = sameHash && account_codec::decode(payload, decoded, codecVersion);
    SecureZeroMemory(digest.data(), digest.size());
    if (!decodedOk) {
        report_account_refused("load_account", codecVersion, sameHash ? "decode" : "hash");
        return LoadStatus::failed;
    }
    prepare_loaded_account(decoded);
    accountId = sqlite3_column_int64(query, 0);
    revision = static_cast<std::uint64_t>(loadedRevision);
    output = decoded;
    return LoadStatus::loaded;
}

[[nodiscard]] LoadStatus load_account_by_id(sqlite3* database,
                                            sqlite3_int64 accountId,
                                            std::uint64_t& revision,
                                            dawn::state::AccountState& output) noexcept {
    Statement statement(
        database,
        "SELECT revision,state_codec,state_blob,state_hash FROM accounts WHERE id=?1;");
    sqlite3_stmt* query = statement.get();
    if (query == nullptr || sqlite3_bind_int64(query, 1, accountId) != SQLITE_OK) {
        return LoadStatus::failed;
    }
    const int step = sqlite3_step(query);
    if (step == SQLITE_DONE) {
        return LoadStatus::missing;
    }
    const int codecVersion = step == SQLITE_ROW ? sqlite3_column_int(query, 1) : 0;
    if (step != SQLITE_ROW || !known_account_codec(codecVersion)) {
        if (step == SQLITE_ROW) {
            report_account_refused("load_account_by_id", codecVersion, "codec");
        }
        return LoadStatus::failed;
    }
    const sqlite3_int64 loadedRevision = sqlite3_column_int64(query, 0);
    const int blobSize = sqlite3_column_bytes(query, 2);
    const int hashSize = sqlite3_column_bytes(query, 3);
    const auto* blob = static_cast<const std::byte*>(sqlite3_column_blob(query, 2));
    const auto* storedHash = static_cast<const std::byte*>(sqlite3_column_blob(query, 3));
    if (loadedRevision < 1 || blobSize <= 0 || blob == nullptr
        || hashSize != static_cast<int>(kDigestSize) || storedHash == nullptr) {
        return LoadStatus::failed;
    }
    const std::span<const std::byte> payload(blob, static_cast<std::size_t>(blobSize));
    std::array<std::byte, kDigestSize> digest{};
    bool sameHash = sha256(payload, digest);
    unsigned difference = 0;
    for (std::size_t index = 0; index < digest.size() && sameHash; ++index) {
        difference |= std::to_integer<unsigned>(digest[index] ^ storedHash[index]);
    }
    dawn::state::AccountState decoded{};
    const bool decodedOk = sameHash && difference == 0
                           && account_codec::decode(payload, decoded, codecVersion)
                           && account_codec::self_test(decoded);
    SecureZeroMemory(digest.data(), digest.size());
    if (!decodedOk) {
        report_account_refused("load_account_by_id", codecVersion, "decode_or_self_test");
        return LoadStatus::failed;
    }
    prepare_loaded_account(decoded);
    revision = static_cast<std::uint64_t>(loadedRevision);
    output = decoded;
    return LoadStatus::loaded;
}

[[nodiscard]] LoadStatus load_account_by_soid(sqlite3* database,
                                              std::uint64_t accountSoid,
                                              sqlite3_int64& accountId,
                                              std::uint64_t& revision,
                                              dawn::state::AccountState& output) noexcept {
    accountId = 0;
    revision = 0;
    output = {};
    Statement statement(database, "SELECT id FROM accounts WHERE account_soid=?1;");
    sqlite3_stmt* query = statement.get();
    if (!bind_u64_blob(query, 1, accountSoid)) {
        return LoadStatus::failed;
    }
    const int step = sqlite3_step(query);
    if (step == SQLITE_DONE) {
        return LoadStatus::missing;
    }
    if (step != SQLITE_ROW) {
        return LoadStatus::failed;
    }
    const sqlite3_int64 resolved = sqlite3_column_int64(query, 0);
    if (resolved <= 0 || sqlite3_step(query) != SQLITE_DONE) {
        return LoadStatus::failed;
    }
    const LoadStatus loaded = load_account_by_id(database, resolved, revision, output);
    if (loaded == LoadStatus::loaded) {
        accountId = resolved;
    }
    return loaded;
}

[[nodiscard]] bool read_u64_blob(sqlite3_stmt* statement,
                                 int column,
                                 std::uint64_t& value) noexcept {
    const int size = sqlite3_column_bytes(statement, column);
    const void* const bytes = sqlite3_column_blob(statement, column);
    if (size != static_cast<int>(sizeof value) || bytes == nullptr) {
        return false;
    }
    std::memcpy(&value, bytes, sizeof value);
    return value != 0;
}

[[nodiscard]] bool copy_column_text(sqlite3_stmt* statement,
                                    int column,
                                    std::span<char> output) noexcept {
    const int size = sqlite3_column_bytes(statement, column);
    const auto* const text = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
    if (size < 0 || text == nullptr || output.empty()
        || static_cast<std::size_t>(size) >= output.size()) {
        return false;
    }
    std::copy_n(text, static_cast<std::size_t>(size), output.begin());
    output[static_cast<std::size_t>(size)] = '\0';
    return size != 0;
}

void prefer_live_account(sqlite3_int64 accountId,
                         dawn::state::AccountState& account,
                         std::uint64_t& revision) noexcept;

[[nodiscard]] bool load_social(sqlite3* database,
                               sqlite3_int64 localAccountId,
                               std::uint64_t localPlatformId,
                               dawn::state::social::Snapshot& output) noexcept {
    output = {};
    output.localPlatformId = localPlatformId;
    output.revision = 1;
    {
        Statement localStatement(database,
                                 "SELECT display_name FROM accounts WHERE id=?1;");
        sqlite3_stmt* localQuery = localStatement.get();
        if (localQuery == nullptr
            || sqlite3_bind_int64(localQuery, 1, localAccountId) != SQLITE_OK
            || sqlite3_step(localQuery) != SQLITE_ROW
            || !copy_column_text(localQuery, 0, output.localDisplayName)
            || sqlite3_step(localQuery) != SQLITE_DONE) {
            return false;
        }
    }
    Statement statement(
        database,
        "SELECT a.id,a.account_soid,p.external_id,a.display_name,"
        "CASE WHEN f.state=2 THEN 3 WHEN f.requester_account_id=?1 THEN 4 ELSE 2 END,"
        "f.updated_at FROM friendships f"
        " JOIN accounts a ON a.id=CASE WHEN f.requester_account_id=?1"
        " THEN f.recipient_account_id ELSE f.requester_account_id END"
        " JOIN platform_identities p ON p.account_id=a.id AND p.provider=?2"
        " WHERE f.requester_account_id=?1 OR f.recipient_account_id=?1"
        " ORDER BY CASE WHEN f.state=2 THEN 0 ELSE 1 END,a.display_name LIMIT ?3;");
    sqlite3_stmt* query = statement.get();
    if (query == nullptr || sqlite3_bind_int64(query, 1, localAccountId) != SQLITE_OK
        || !bind_text(query, 2, kProvider)
        || sqlite3_bind_int(query, 3, static_cast<int>(output.remotes.size())) != SQLITE_OK) {
        return false;
    }
    for (;;) {
        const int step = sqlite3_step(query);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            return false;
        }
        // stop here instead of failing: more than kRemoteCapacity friends used to mean a 503 and a
        // client stuck on the placeholder persona. A long list must degrade to a shorter one
        if (output.remoteCount == output.remotes.size()) {
            break;
        }
        auto& remote = output.remotes[output.remoteCount];
        const sqlite3_int64 remoteAccountId = sqlite3_column_int64(query, 0);
        if (remoteAccountId <= 0 || !read_u64_blob(query, 1, remote.accountSoid)
            || !read_u64_blob(query, 2, remote.platformId)
            || !copy_column_text(query, 3, remote.displayName)) {
            return false;
        }
        remote.relationship =
            static_cast<dawn::state::social::Relationship>(sqlite3_column_int(query, 4));
        const sqlite3_int64 friendshipRevision = sqlite3_column_int64(query, 5);
        if (remote.relationship == dawn::state::social::Relationship::friendAccount) {
            dawn::state::AccountState account{};
            std::uint64_t profileRevision = 0;
            if (load_account_by_id(database,
                                   remoteAccountId,
                                   profileRevision,
                                   account)
                != LoadStatus::loaded
                || account.primarySoid != remote.accountSoid) {
                return false;
            }
            // loaders clear the persisted selection, which gave the wrong class icon and emblem in rosters
            prefer_live_account(remoteAccountId, account, profileRevision);
            remote.profileRevision = profileRevision;
            const std::uint64_t selected = dawn::state::account::selected_character_soid(account);
            const std::uint64_t presented =
                selected != 0 ? selected : dawn::state::account::banner_character_soid(account);
            for (std::size_t characterIndex = 0; characterIndex < account.characterCount;
                 ++characterIndex) {
                const auto& character = account.characters[characterIndex];
                if (character.soid != presented) {
                    continue;
                }
                remote.selectedCharacterSoid = character.soid;
                remote.characterLevel = character.level;
                remote.characterClass = static_cast<std::uint8_t>(character.characterClass);
                std::int32_t power = 0;
                if (dawn::state::equipment::light::resolution::character_light(
                        account, characterIndex, power)
                    && power >= 0) {
                    remote.power = power;
                }
                constexpr std::size_t kEmblemSlot = static_cast<std::size_t>(
                    dawn::state::account::inventory::EquipmentSlot::emblem);
                if (const auto& emblem = character.equipment.slots[kEmblemSlot];
                    emblem.has_value()) {
                    dawn::state::build_data::items::Definition definition{};
                    if (dawn::state::build_data::items::find_hash(
                            emblem->definitionHash, definition)) {
                        remote.emblemDefinition = definition.definitionIndex;
                    }
                }
                constexpr std::size_t kShipSlot = static_cast<std::size_t>(
                    dawn::state::account::inventory::EquipmentSlot::ship);
                if (const auto& ship = character.equipment.slots[kShipSlot]; ship.has_value()) {
                    remote.shipInstanceSoid = ship->instanceSoid;
                    remote.shipDefinitionHash = ship->definitionHash;
                }
                break;
            }
            output.revision = (std::max)(output.revision, profileRevision);
        }
        const sqlite3_int64 revision = friendshipRevision;
        if (revision > 0) {
            output.revision = (std::max)(output.revision,
                                         static_cast<std::uint64_t>(revision));
        }
        ++output.remoteCount;
    }
    return dawn::state::social::valid(output);
}

[[nodiscard]] bool initialize_social(sqlite3* database,
                                     sqlite3_int64 localAccountId,
                                     std::uint64_t localPlatformId,
                                     dawn::state::social::Snapshot& output) noexcept {
    return load_social(database, localAccountId, localPlatformId, output);
}

template <typename... Args>
[[nodiscard]] bool format_message(std::span<char> output,
                                  std::size_t& written,
                                  const char* format,
                                  Args... args) noexcept {
    written = 0;
    if (output.empty()) {
        return false;
    }
    const int count = std::snprintf(output.data(), output.size(), format, args...);
    if (count < 0 || static_cast<std::size_t>(count) >= output.size()) {
        output.front() = '\0';
        return false;
    }
    written = static_cast<std::size_t>(count);
    return true;
}

[[nodiscard]] bool valid_discord_user_id(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 20
           && std::all_of(value.cbegin(), value.cend(), [](char character) noexcept {
                  return character >= '0' && character <= '9';
              });
}

[[nodiscard]] bool valid_account_name(std::string_view value) noexcept {
    if (value.size() < 3 || value.size() >= kAccountNameCapacity || value.front() == ' '
        || value.back() == ' ') {
        return false;
    }
    return std::all_of(value.cbegin(), value.cend(), [](char character) noexcept {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == ' ' || character == '_'
               || character == '-';
    });
}

[[nodiscard]] bool canonical_link_code(
    std::string_view input,
    std::array<char, kCanonicalLinkCodeSize>& output) noexcept {
    output = {};
    std::size_t count = 0;
    for (const char character : input) {
        if (character == '-') {
            continue;
        }
        if (count == output.size()) {
            return false;
        }
        const char upper = character >= 'a' && character <= 'z'
                               ? static_cast<char>(character - ('a' - 'A'))
                               : character;
        if (kLinkAlphabet.find(upper) == std::string_view::npos) {
            return false;
        }
        output[count++] = upper;
    }
    return count == output.size();
}

[[nodiscard]] bool generate_link_code(
    std::array<char, kCanonicalLinkCodeSize>& canonical,
    std::array<char, kDiscordLinkCodeCapacity>& display) noexcept {
    std::array<unsigned char, kCanonicalLinkCodeSize> random{};
    if (BCryptGenRandom(nullptr,
                        random.data(),
                        static_cast<ULONG>(random.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG)
        < 0) {
        return false;
    }
    canonical = {};
    display = {};
    for (std::size_t index = 0; index < canonical.size(); ++index) {
        canonical[index] = kLinkAlphabet[random[index] % kLinkAlphabet.size()];
        const std::size_t destination = index < 4 ? index : index + 1;
        display[destination] = canonical[index];
    }
    display[4] = '-';
    SecureZeroMemory(random.data(), random.size());
    return true;
}

[[nodiscard]] bool account_for_platform(sqlite3* database,
                                        std::uint64_t platformId,
                                        sqlite3_int64& accountId,
                                        std::span<char> name) noexcept {
    Statement statement(
        database,
        "SELECT a.id,a.display_name FROM accounts a JOIN platform_identities p"
        " ON p.account_id=a.id WHERE p.provider=?1 AND p.external_id=?2;");
    sqlite3_stmt* query = statement.get();
    if (!bind_text(query, 1, kProvider) || !bind_u64_blob(query, 2, platformId)
        || sqlite3_step(query) != SQLITE_ROW
        || !copy_column_text(query, 1, name)) {
        accountId = 0;
        return false;
    }
    accountId = sqlite3_column_int64(query, 0);
    return accountId > 0 && sqlite3_step(query) == SQLITE_DONE;
}

[[nodiscard]] bool account_for_discord(sqlite3* database,
                                       std::string_view discordUserId,
                                       sqlite3_int64& accountId,
                                       std::span<char> name) noexcept {
    Statement statement(
        database,
        "SELECT a.id,a.display_name FROM discord_links d JOIN accounts a ON a.id=d.account_id"
        " WHERE d.discord_user_id=?1;");
    sqlite3_stmt* query = statement.get();
    if (!bind_text(query, 1, discordUserId) || sqlite3_step(query) != SQLITE_ROW
        || !copy_column_text(query, 1, name)) {
        accountId = 0;
        return false;
    }
    accountId = sqlite3_column_int64(query, 0);
    return accountId > 0 && sqlite3_step(query) == SQLITE_DONE;
}

[[nodiscard]] bool account_for_name(sqlite3* database,
                                    std::string_view name,
                                    sqlite3_int64& accountId,
                                    std::span<char> canonicalName) noexcept {
    Statement statement(
        database,
        "SELECT id,display_name FROM accounts WHERE display_name=?1 COLLATE NOCASE;");
    sqlite3_stmt* query = statement.get();
    if (!bind_text(query, 1, name) || sqlite3_step(query) != SQLITE_ROW
        || !copy_column_text(query, 1, canonicalName)) {
        accountId = 0;
        return false;
    }
    accountId = sqlite3_column_int64(query, 0);
    return accountId > 0 && sqlite3_step(query) == SQLITE_DONE;
}

[[nodiscard]] bool relation(sqlite3* database,
                            sqlite3_int64 localAccountId,
                            sqlite3_int64 targetAccountId,
                            sqlite3_int64& requester,
                            sqlite3_int64& recipient,
                            int& state) noexcept {
    Statement statement(
        database,
        "SELECT requester_account_id,recipient_account_id,state FROM friendships WHERE"
        " (requester_account_id=?1 AND recipient_account_id=?2) OR"
        " (requester_account_id=?2 AND recipient_account_id=?1);");
    sqlite3_stmt* query = statement.get();
    if (query == nullptr || sqlite3_bind_int64(query, 1, localAccountId) != SQLITE_OK
        || sqlite3_bind_int64(query, 2, targetAccountId) != SQLITE_OK) {
        return false;
    }
    const int step = sqlite3_step(query);
    if (step == SQLITE_DONE) {
        requester = 0;
        recipient = 0;
        state = 0;
        return true;
    }
    if (step != SQLITE_ROW) {
        return false;
    }
    requester = sqlite3_column_int64(query, 0);
    recipient = sqlite3_column_int64(query, 1);
    state = sqlite3_column_int(query, 2);
    return requester > 0 && recipient > 0 && state >= 1 && state <= 3
           && sqlite3_step(query) == SQLITE_DONE;
}

[[nodiscard]] bool save_account_locked(sqlite3_int64 accountId,
                                       std::uint64_t& revision,
                                       const dawn::state::AccountState& account,
                                       const WeaponAcquireRequest* receipt = nullptr,
                                       const WeaponAcquireResult* result = nullptr,
                                       std::string_view requestText = {}) noexcept {
    if (g_database == nullptr || accountId <= 0 || account.primarySoid == 0) {
        return false;
    }
    std::vector<std::byte> encoded;
    std::array<std::byte, kDigestSize> digest{};
    if (!account_codec::encode(account, encoded) || !sha256(encoded, digest)
        || !execute(g_database, "BEGIN IMMEDIATE;")) {
        return false;
    }
    Statement statement(
        g_database,
        "UPDATE accounts SET account_soid=?1,revision=revision+1,state_codec=?2,state_blob=?3,"
        "state_hash=?4,updated_at=CAST(strftime('%s','now') AS INTEGER) "
        "WHERE id=?5 AND state_hash<>?4;");
    sqlite3_stmt* update = statement.get();
    bool success = bind_u64_blob(update, 1, account.primarySoid)
                   && sqlite3_bind_int(update, 2, account_codec::kVersion) == SQLITE_OK
                   && bind_bytes(update, 3, encoded) && bind_bytes(update, 4, digest)
                   && sqlite3_bind_int64(update, 5, accountId) == SQLITE_OK
                   && sqlite3_step(update) == SQLITE_DONE;
    const int changes = success ? sqlite3_changes(g_database) : -1;
    if (success && receipt != nullptr && result != nullptr) {
        Statement insert(g_database, "INSERT INTO weapon_acquire_receipts VALUES(?1,?2,?3,?4,?5,?6,?7);");
        auto* query = insert.get();
        success = query != nullptr && sqlite3_bind_int64(query, 1, accountId) == SQLITE_OK
            && bind_u64_blob(query, 2, receipt->requestId) && bind_text(query, 3, requestText)
            && bind_u64_blob(query, 4, result->instanceSoid)
            && bind_u64_blob(query, 5, result->characterSoid)
            && sqlite3_bind_int64(query, 6, result->definitionHash) == SQLITE_OK
            && sqlite3_bind_int64(query, 7, static_cast<sqlite3_int64>(revision + (changes == 1))) == SQLITE_OK
            && sqlite3_step(query) == SQLITE_DONE;
    }
    success = success && (changes == 0 || changes == 1) && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
    } else if (changes == 1) {
        ++revision;
    }
    SecureZeroMemory(digest.data(), digest.size());
    return success;
}

[[nodiscard]] bool save_activity_for_account_locked(
    sqlite3_int64 accountId,
    std::uint64_t& revision,
    const dawn::state::activity::ActivityState& activity) noexcept {
    if (g_database == nullptr || accountId <= 0) {
        return false;
    }
    std::uint64_t ownerAccountSoid = 0;
    for (const auto& record : activity.sessions) {
        if (record.occupied) {
            ownerAccountSoid = record.ownerAccountSoid;
            break;
        }
    }
    std::vector<std::byte> encoded;
    std::array<std::byte, kDigestSize> digest{};
    if ((ownerAccountSoid != 0 && !activity_codec::self_test(activity, ownerAccountSoid))
        || !activity_codec::encode(activity, encoded) || !sha256(encoded, digest)
        || !execute(g_database, "BEGIN IMMEDIATE;")) {
        return false;
    }
    Statement statement(
        g_database,
        "INSERT INTO activity_checkpoints(account_id,revision,state_codec,state_blob,state_hash,"
        "created_at,updated_at) VALUES(?1,1,?2,?3,?4,"
        "CAST(strftime('%s','now') AS INTEGER),CAST(strftime('%s','now') AS INTEGER)) "
        "ON CONFLICT(account_id) DO UPDATE SET revision=activity_checkpoints.revision+1,"
        "state_codec=excluded.state_codec,state_blob=excluded.state_blob,"
        "state_hash=excluded.state_hash,updated_at=excluded.updated_at "
        "WHERE activity_checkpoints.state_hash<>excluded.state_hash;");
    sqlite3_stmt* update = statement.get();
    bool success = sqlite3_bind_int64(update, 1, accountId) == SQLITE_OK
                   && sqlite3_bind_int(update, 2, activity_codec::kVersion) == SQLITE_OK
                   && bind_bytes(update, 3, encoded) && bind_bytes(update, 4, digest)
                   && sqlite3_step(update) == SQLITE_DONE;
    const int changes = success ? sqlite3_changes(g_database) : -1;
    success = success && (changes == 0 || changes == 1) && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
    } else if (changes == 1) {
        ++revision;
    }
    SecureZeroMemory(digest.data(), digest.size());
    return success;
}

enum class DeviceStatus : std::uint8_t {
    missing,
    matched,
    failed,
};

[[nodiscard]] DeviceStatus device_account_locked(
    std::uint64_t platformId,
    std::span<const std::byte> publicKey,
    sqlite3_int64& accountId) noexcept {
    accountId = 0;
    Statement statement(
        g_database,
        "SELECT account_id,public_key FROM device_credentials WHERE platform_id=?1;");
    sqlite3_stmt* query = statement.get();
    if (!bind_u64_blob(query, 1, platformId)) {
        return DeviceStatus::failed;
    }
    const int step = sqlite3_step(query);
    if (step == SQLITE_DONE) {
        return DeviceStatus::missing;
    }
    const int keySize = sqlite3_column_bytes(query, 1);
    const auto* storedKey = static_cast<const std::byte*>(sqlite3_column_blob(query, 1));
    if (step != SQLITE_ROW || keySize != static_cast<int>(publicKey.size())
        || storedKey == nullptr
        || !std::equal(publicKey.begin(), publicKey.end(), storedKey)) {
        return DeviceStatus::failed;
    }
    accountId = sqlite3_column_int64(query, 0);
    if (accountId <= 0 || sqlite3_step(query) != SQLITE_DONE) {
        accountId = 0;
        return DeviceStatus::failed;
    }
    Statement touch(
        g_database,
        "UPDATE device_credentials SET last_seen=CAST(strftime('%s','now') AS INTEGER)"
        " WHERE platform_id=?1;");
    sqlite3_stmt* update = touch.get();
    return bind_u64_blob(update, 1, platformId) && sqlite3_step(update) == SQLITE_DONE
               ? DeviceStatus::matched
               : DeviceStatus::failed;
}

[[nodiscard]] bool next_namespace_locked(std::uint32_t& value) noexcept {
    value = 0;
    Statement statement(g_database,
                        "SELECT COALESCE(MAX(namespace),0)+1 FROM account_namespaces;");
    sqlite3_stmt* query = statement.get();
    if (query == nullptr || sqlite3_step(query) != SQLITE_ROW) {
        return false;
    }
    const sqlite3_int64 candidate = sqlite3_column_int64(query, 0);
    if (candidate < 1 || candidate > kMaximumAccountNamespace
        || sqlite3_step(query) != SQLITE_DONE) {
        return false;
    }
    value = static_cast<std::uint32_t>(candidate);
    return true;
}

[[nodiscard]] bool starter_account(std::uint32_t accountNamespace,
                                   dawn::state::AccountState& output) noexcept {
    if (accountNamespace == 0 || accountNamespace > kMaximumAccountNamespace) {
        return false;
    }
    output = g_starterSeed;
    output.primarySoid = kAccountSoidBase
                         | (static_cast<std::uint64_t>(accountNamespace) << 20U);
    for (std::size_t index = 0; index < output.characterCount; ++index) {
        output.characters[index].soid = output.primarySoid + index + 1;
    }
    return dawn::state::prepare_account_runtime(output)
           && account_codec::self_test(output);
}

[[nodiscard]] bool create_device_account_locked(
    std::uint64_t deviceId,
    std::uint64_t platformId,
    std::span<const std::byte> publicKey,
    sqlite3_int64& accountId) noexcept {
    accountId = 0;
    std::uint32_t accountNamespace = 0;
    dawn::state::AccountState account{};
    std::vector<std::byte> encoded;
    std::array<std::byte, kDigestSize> digest{};
    std::array<char, kAccountNameCapacity> displayName{};
    const bool prepared = next_namespace_locked(accountNamespace)
                          && starter_account(accountNamespace, account)
                          && account_codec::encode(account, encoded)
                          && sha256(encoded, digest);
    const int nameSize = prepared
                             ? std::snprintf(displayName.data(),
                                             displayName.size(),
                                             "Player-%04X-%u",
                                             static_cast<unsigned>(platformId & 0xFFFFU),
                                             accountNamespace)
                             : -1;
    if (!prepared || nameSize <= 0
        || static_cast<std::size_t>(nameSize) >= displayName.size()
        || !execute(g_database, "BEGIN IMMEDIATE;")) {
        SecureZeroMemory(digest.data(), digest.size());
        return false;
    }

    Statement accountStatement(
        g_database,
        "INSERT INTO accounts(account_soid,display_name,revision,state_codec,state_blob,"
        "state_hash,created_at,updated_at) VALUES(?1,?2,1,?3,?4,?5,"
        "CAST(strftime('%s','now') AS INTEGER),CAST(strftime('%s','now') AS INTEGER));");
    sqlite3_stmt* insertAccount = accountStatement.get();
    bool success = bind_u64_blob(insertAccount, 1, account.primarySoid)
                   && bind_text(insertAccount, 2, displayName.data())
                   && sqlite3_bind_int(insertAccount, 3, account_codec::kVersion) == SQLITE_OK
                   && bind_bytes(insertAccount, 4, encoded)
                   && bind_bytes(insertAccount, 5, digest)
                   && sqlite3_step(insertAccount) == SQLITE_DONE;
    if (success) {
        accountId = sqlite3_last_insert_rowid(g_database);
        Statement identityStatement(
            g_database,
            "INSERT INTO platform_identities(provider,external_id,account_id,created_at)"
            " VALUES(?1,?2,?3,CAST(strftime('%s','now') AS INTEGER));");
        sqlite3_stmt* insertIdentity = identityStatement.get();
        success = bind_text(insertIdentity, 1, kProvider)
                  && bind_u64_blob(insertIdentity, 2, platformId)
                  && sqlite3_bind_int64(insertIdentity, 3, accountId) == SQLITE_OK
                  && sqlite3_step(insertIdentity) == SQLITE_DONE;
    }
    if (success) {
        Statement namespaceStatement(
            g_database,
            "INSERT INTO account_namespaces(account_id,namespace) VALUES(?1,?2);");
        sqlite3_stmt* insertNamespace = namespaceStatement.get();
        success = insertNamespace != nullptr
                  && sqlite3_bind_int64(insertNamespace, 1, accountId) == SQLITE_OK
                  && sqlite3_bind_int(insertNamespace,
                                      2,
                                      static_cast<int>(accountNamespace)) == SQLITE_OK
                  && sqlite3_step(insertNamespace) == SQLITE_DONE;
    }
    if (success) {
        Statement credentialStatement(
            g_database,
            "INSERT INTO device_credentials(platform_id,public_key,account_id,created_at,last_seen)"
            " VALUES(?1,?2,?3,CAST(strftime('%s','now') AS INTEGER),"
            "CAST(strftime('%s','now') AS INTEGER));");
        sqlite3_stmt* insertCredential = credentialStatement.get();
        success = bind_u64_blob(insertCredential, 1, deviceId)
                  && bind_bytes(insertCredential, 2, publicKey)
                  && sqlite3_bind_int64(insertCredential, 3, accountId) == SQLITE_OK
                  && sqlite3_step(insertCredential) == SQLITE_DONE;
    }
    success = success && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
        accountId = 0;
    }
    SecureZeroMemory(digest.data(), digest.size());
    return success;
}

// only adopts an account whose key record staff deleted, so a stranger's key can't take over a Steam account
[[nodiscard]] bool adopt_released_account_locked(std::uint64_t deviceId,
                                                 std::span<const std::byte> publicKey,
                                                 sqlite3_int64 accountId) noexcept {
    if (deviceId == 0 || accountId <= 0 || !execute(g_database, "BEGIN IMMEDIATE;")) {
        return false;
    }
    Statement existing(g_database, "SELECT 1 FROM device_credentials WHERE account_id=?1;");
    sqlite3_stmt* existingQuery = existing.get();
    bool success = existingQuery != nullptr
                   && sqlite3_bind_int64(existingQuery, 1, accountId) == SQLITE_OK
                   && sqlite3_step(existingQuery) == SQLITE_DONE;
    if (success) {
        Statement credentialStatement(
            g_database,
            "INSERT INTO device_credentials(platform_id,public_key,account_id,created_at,last_seen)"
            " VALUES(?1,?2,?3,CAST(strftime('%s','now') AS INTEGER),"
            "CAST(strftime('%s','now') AS INTEGER));");
        sqlite3_stmt* insertCredential = credentialStatement.get();
        success = bind_u64_blob(insertCredential, 1, deviceId)
                  && bind_bytes(insertCredential, 2, publicKey)
                  && sqlite3_bind_int64(insertCredential, 3, accountId) == SQLITE_OK
                  && sqlite3_step(insertCredential) == SQLITE_DONE;
    }
    success = success && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
    }
    return success;
}

[[nodiscard]] bool bind_platform_identity_locked(std::uint64_t platformId,
                                                 sqlite3_int64 accountId) noexcept {
    if (platformId == 0 || accountId <= 0 || !execute(g_database, "BEGIN IMMEDIATE;")) {
        return false;
    }
    Statement collision(
        g_database,
        "SELECT account_id FROM platform_identities WHERE provider=?1 AND external_id=?2;");
    sqlite3_stmt* collisionQuery = collision.get();
    bool success = bind_text(collisionQuery, 1, kProvider)
                   && bind_u64_blob(collisionQuery, 2, platformId);
    const int collisionStep = success ? sqlite3_step(collisionQuery) : SQLITE_ERROR;
    if (collisionStep == SQLITE_ROW) {
        success = sqlite3_column_int64(collisionQuery, 0) == accountId
                  && sqlite3_step(collisionQuery) == SQLITE_DONE;
    } else {
        success = collisionStep == SQLITE_DONE;
    }
    if (success && collisionStep == SQLITE_DONE) {
        Statement update(
            g_database,
            "UPDATE platform_identities SET external_id=?1"
            " WHERE provider=?2 AND account_id=?3;");
        sqlite3_stmt* statement = update.get();
        success = bind_u64_blob(statement, 1, platformId)
                  && bind_text(statement, 2, kProvider)
                  && sqlite3_bind_int64(statement, 3, accountId) == SQLITE_OK
                  && sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(g_database) == 1;
    }
    success = success && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
    }
    return success;
}

[[nodiscard]] RuntimeRecord* find_runtime_record(std::uint64_t accountSoid,
                                                 std::uint64_t platformId) noexcept {
    for (const auto& record : g_runtimeRecords) {
        if (record->account.primarySoid == accountSoid
            && record->platformId == platformId) {
            return record.get();
        }
    }
    return nullptr;
}

void sync_runtime_account(const dawn::state::AccountState& account) noexcept {
    for (auto& record : g_runtimeRecords) {
        if (record != nullptr && record->account.primarySoid == account.primarySoid) {
            record->account = account;
        }
    }
}

[[nodiscard]] bool erase_character(dawn::state::AccountState& account,
                                   std::uint64_t characterSoid) noexcept {
    std::size_t found = account.characterCount;
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].soid == characterSoid) {
            found = index;
            break;
        }
    }
    if (found >= account.characterCount) {
        return false;
    }
    for (std::size_t index = found + 1; index < account.characterCount; ++index) {
        account.characters[index - 1] = account.characters[index];
    }
    account.characters[account.characterCount - 1] = {};
    --account.characterCount;
    // no auto-select: inventing a pick would make the resync seat a character the client never chose
    return true;
}

[[nodiscard]] bool character_uses_identity(const dawn::state::CharacterState& character,
                                           std::uint64_t soid) noexcept {
    if (character.soid == soid) {
        return true;
    }
    for (const auto& item : character.equipment.slots) {
        if (item.has_value() && item->instanceSoid == soid) {
            return true;
        }
    }
    for (std::size_t index = 0; index < character.inventory.count; ++index) {
        if (character.inventory.values[index].instanceSoid == soid) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool identity_in_use(const dawn::state::AccountState& account,
                                   const dawn::state::CharacterState& pending,
                                   std::uint64_t soid) noexcept {
    if (soid == 0 || soid == account.primarySoid) {
        return true;
    }
    for (std::size_t index = 0; index < account.profileItemCount; ++index) {
        if (account.profileItems[index].instanceSoid == soid) {
            return true;
        }
    }
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (character_uses_identity(account.characters[index], soid)) {
            return true;
        }
    }
    for (std::size_t index = 0; index < account.vault.count; ++index) {
        if (account.vault.values[index].instanceSoid == soid) {
            return true;
        }
    }
    return character_uses_identity(pending, soid);
}

// subclass items in build 86657 by class (titan, hunter, warlock); Stasis ships later
constexpr std::array<std::array<std::uint32_t, 3>, 3> kClassSubclasses{{
    {0xB0554739U, 0xB920CE9AU, 0xC99B33E9U},
    {0x4F91DC97U, 0xD8B8D1FCU, 0xC0483D8BU},
    {0x686A154AU, 0xCF88FEA5U, 0xE7BC88B0U},
}};

[[nodiscard]] bool grant_class_subclasses(dawn::state::AccountState& account) noexcept {
    namespace inventory = dawn::state::account::inventory;
    static std::uint16_t counter = 0;
    const std::uint64_t middle =
        (static_cast<std::uint64_t>(GetTickCount64()) << 16U) & 0x0000FFFFFFFF0000ULL;
    bool granted = false;
    const std::size_t count = (std::min)(account.characterCount, account.characters.size());
    for (std::size_t index = 0; index < count; ++index) {
        dawn::state::CharacterState& character = account.characters[index];
        const auto classIndex = static_cast<std::size_t>(character.characterClass);
        if (classIndex >= kClassSubclasses.size()) {
            continue;
        }
        const std::optional<inventory::Item>& equipped =
            character.equipment.slots[static_cast<std::size_t>(inventory::EquipmentSlot::subclass)];
        for (const std::uint32_t hash : kClassSubclasses[classIndex]) {
            bool owned = equipped.has_value() && equipped->definitionHash == hash;
            for (std::size_t item = 0; !owned && item < character.inventory.count; ++item) {
                owned = character.inventory.values[item].definitionHash == hash;
            }
            if (owned) {
                continue;
            }
            if (character.inventory.count >= character.inventory.values.size()
                || character.nextInventorySerial
                       >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
                break;
            }
            std::uint64_t soid = 0;
            for (std::uint32_t attempt = 0; attempt < 0x10000U && soid == 0; ++attempt) {
                const std::uint64_t candidate = 0x4DA0000000000000ULL | middle | ++counter;
                if (!identity_in_use(account, character, candidate)) {
                    soid = candidate;
                }
            }
            if (soid == 0) {
                return granted;
            }
            inventory::Item item{};
            item.instanceSoid = soid;
            item.definitionHash = hash;
            item.level = equipped.has_value() ? equipped->level : 0;
            item.quantity = 1;
            item.mutationSerial = static_cast<std::int32_t>(character.nextInventorySerial++);
            character.inventory.values[character.inventory.count++] = item;
            granted = true;
        }
    }
    return granted;
}

[[nodiscard]] std::size_t grant_listed_items(dawn::state::AccountState& account) noexcept {
    namespace inventory = dawn::state::account::inventory;
    static std::array<std::uint32_t, 64> hashes{};
    static std::size_t hashCount = 0;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        std::array<char, 4096> text{};
        if (dawn::core::path::read_artifact_text(L"item_grants.txt", text)) {
            dawn::core::rule_text::Cursor cursor{text.data()};
            while (cursor.seek_field() && hashCount < hashes.size()) {
                hashes[hashCount++] = cursor.read_hex();
            }
        }
    }
    static std::uint16_t counter = 0;
    const std::uint64_t middle =
        (static_cast<std::uint64_t>(GetTickCount64()) << 16U) & 0x0000FFFFFFFF0000ULL;
    const std::size_t characterCount = (std::min)(account.characterCount, account.characters.size());
    std::size_t granted = 0;
    for (std::size_t listed = 0; listed < hashCount; ++listed) {
        const std::uint32_t hash = hashes[listed];
        bool owned = false;
        for (std::size_t index = 0; !owned && index < characterCount; ++index) {
            const dawn::state::CharacterState& character = account.characters[index];
            for (const auto& slot : character.equipment.slots) {
                owned = owned || (slot.has_value() && slot->definitionHash == hash);
            }
            for (std::size_t item = 0; !owned && item < character.inventory.count; ++item) {
                owned = character.inventory.values[item].definitionHash == hash;
            }
        }
        for (std::size_t index = 0; !owned && index < characterCount; ++index) {
            dawn::state::CharacterState& character = account.characters[index];
            if (character.inventory.count >= character.inventory.values.size()
                || character.nextInventorySerial
                       >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
                continue;
            }
            std::uint64_t soid = 0;
            for (std::uint32_t attempt = 0; attempt < 0x10000U && soid == 0; ++attempt) {
                const std::uint64_t candidate = 0x4DA0000000000000ULL | middle | ++counter;
                if (!identity_in_use(account, character, candidate)) {
                    soid = candidate;
                }
            }
            if (soid == 0) {
                return granted;
            }
            std::int32_t level = 0;
            for (const auto& slot : character.equipment.slots) {
                if (slot.has_value() && slot->level > level) {
                    level = slot->level;
                }
            }
            inventory::Item item{};
            item.instanceSoid = soid;
            item.definitionHash = hash;
            item.level = level;
            item.quantity = 1;
            item.mutationSerial = static_cast<std::int32_t>(character.nextInventorySerial++);
            character.inventory.values[character.inventory.count++] = item;
            ++granted;
            owned = true;
        }
    }
    return granted;
}

// a verbatim template clone duplicates item identities (account::valid refuses) and carries
// unseeded serials (family-4 encoder refuses), so give it fresh ids and sequential serials
[[nodiscard]] bool append_character(dawn::state::AccountState& account,
                                    const dawn::state::CharacterCreateRequest& request,
                                    std::uint64_t& createdSoid) noexcept {
    createdSoid = 0;
    if (account.characterCount >= account.characters.size()
        || request.characterClass > static_cast<std::uint8_t>(dawn::state::CharacterClass::warlock)
        || request.race > static_cast<std::uint8_t>(dawn::state::CharacterRace::exo)
        || request.gender > static_cast<std::uint8_t>(dawn::state::CharacterGender::female)) {
        return false;
    }
    std::size_t chosen = g_starterSeed.characterCount;
    for (std::size_t index = 0; index < g_starterSeed.characterCount; ++index) {
        const auto& seed = g_starterSeed.characters[index];
        if (static_cast<std::uint8_t>(seed.characterClass) != request.characterClass) {
            continue;
        }
        if (chosen == g_starterSeed.characterCount) {
            chosen = index;
        }
        if (static_cast<std::uint8_t>(seed.race) == request.race
            && static_cast<std::uint8_t>(seed.gender) == request.gender) {
            chosen = index;
            break;
        }
    }
    if (chosen == g_starterSeed.characterCount) {
        return false;
    }
    dawn::state::CharacterState created = g_starterSeed.characters[chosen];
    created.soid = 0;
    for (auto& item : created.equipment.slots) {
        if (item.has_value()) {
            item->instanceSoid = 0;
        }
    }
    for (std::size_t index = 0; index < created.inventory.count; ++index) {
        created.inventory.values[index].instanceSoid = 0;
    }

    const std::uint64_t middle =
        (static_cast<std::uint64_t>(GetTickCount64()) << 16U) & 0x0000FFFFFFFF0000ULL;
    static std::uint16_t counter = 0;
    const auto next_identity = [&](std::uint64_t prefix) noexcept -> std::uint64_t {
        for (std::uint32_t attempt = 0; attempt < 0x10000U; ++attempt) {
            const std::uint64_t candidate = prefix | middle | ++counter;
            if (!identity_in_use(account, created, candidate)) {
                return candidate;
            }
        }
        return 0;
    };
    created.soid = next_identity(0x0DA0000000000000ULL);
    if (created.soid == 0) {
        return false;
    }
    for (auto& item : created.equipment.slots) {
        if (item.has_value() && (item->instanceSoid = next_identity(0x4DA0000000000000ULL)) == 0) {
            return false;
        }
    }
    for (std::size_t index = 0; index < created.inventory.count; ++index) {
        auto& item = created.inventory.values[index];
        if ((item.instanceSoid = next_identity(0x4DA0000000000000ULL)) == 0) {
            return false;
        }
    }
    seed_character_serials(created);
    created.selected = false;
    created.characterClass = static_cast<dawn::state::CharacterClass>(request.characterClass);
    created.race = static_cast<dawn::state::CharacterRace>(request.race);
    created.gender = static_cast<dawn::state::CharacterGender>(request.gender);
    created.accepted = true;
    created.previewAvailable = true;
    created.identityHeader = request.identityHeader;
    created.hasIdentityHeader = request.hasIdentityHeader;
    account.characters[account.characterCount] = created;
    ++account.characterCount;
    (void)grant_class_subclasses(account);
    createdSoid = created.soid;
    return true;
}

struct CustomizeTarget final {
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t expiresAtTick{};
};

constexpr std::uint64_t kCustomizeLifetimeMilliseconds = 30ULL * 60ULL * 1000ULL;
std::array<CustomizeTarget, 64> g_customizeTargets{};

[[nodiscard]] CustomizeTarget* live_customize_target(std::uint64_t accountSoid) noexcept {
    const std::uint64_t now = GetTickCount64();
    CustomizeTarget* found = nullptr;
    for (CustomizeTarget& target : g_customizeTargets) {
        if (target.accountSoid == 0) {
            continue;
        }
        if (target.expiresAtTick < now) {
            target = {};
            continue;
        }
        if (target.accountSoid == accountSoid) {
            found = &target;
        }
    }
    return found;
}

[[nodiscard]] bool apply_customization(dawn::state::AccountState& account,
                                       const dawn::state::CharacterCreateRequest& request,
                                       std::uint64_t characterSoid,
                                       std::uint64_t& appliedSoid) noexcept {
    appliedSoid = 0;
    if (characterSoid == 0
        || request.race > static_cast<std::uint8_t>(dawn::state::CharacterRace::exo)
        || request.gender > static_cast<std::uint8_t>(dawn::state::CharacterGender::female)) {
        return false;
    }
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        dawn::state::CharacterState& character = account.characters[index];
        if (character.soid != characterSoid) {
            continue;
        }
        character.race = static_cast<dawn::state::CharacterRace>(request.race);
        character.gender = static_cast<dawn::state::CharacterGender>(request.gender);
        if (request.hasIdentityHeader) {
            character.identityHeader = request.identityHeader;
            character.hasIdentityHeader = true;
        }
        appliedSoid = characterSoid;
        return true;
    }
    return false;
}

void prefer_live_account(sqlite3_int64 accountId,
                         dawn::state::AccountState& account,
                         std::uint64_t& revision) noexcept {
    for (const auto& record : g_runtimeRecords) {
        if (record != nullptr && record->accountId == accountId
            && record->account.primarySoid != 0) {
            account = record->account;
            revision = record->revision;
            return;
        }
    }
}

[[nodiscard]] bool platform_account_locked(std::uint64_t platformId,
                                           dawn::state::AccountState& account) noexcept {
    account = {};
    sqlite3_int64 accountId = 0;
    std::array<char, kAccountNameCapacity> displayName{};
    if (g_database == nullptr || platformId == 0
        || !account_for_platform(g_database, platformId, accountId, displayName)) {
        return false;
    }
    for (const auto& record : g_runtimeRecords) {
        if (record != nullptr && record->accountId == accountId) {
            account = record->account;
            return true;
        }
    }
    std::uint64_t revision = 0;
    return load_account_by_id(g_database, accountId, revision, account) == LoadStatus::loaded;
}

[[nodiscard]] bool save_runtime_account(
    void* context,
    const dawn::state::AccountState& account) noexcept {
    auto* const record = static_cast<RuntimeRecord*>(context);
    if (record == nullptr || account.primarySoid != record->account.primarySoid) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    const bool success = save_account_locked(record->accountId, record->revision, account);
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

[[nodiscard]] bool save_runtime_activity(
    void* context,
    const dawn::state::activity::ActivityState& activity) noexcept {
    auto* const record = static_cast<RuntimeRecord*>(context);
    if (record == nullptr) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    const bool success = save_activity_for_account_locked(
        record->accountId, record->activityRevision, activity);
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

[[nodiscard]] dawn::state::AccountRuntimeContext runtime_context(
    RuntimeRecord& record) noexcept {
    return dawn::state::AccountRuntimeContext{
        &record.account,
        &record.activity,
        dawn::state::AccountPersistence{&record, &save_runtime_account},
        dawn::state::ActivityPersistence{&record, &save_runtime_activity},
    };
}

constexpr std::uint64_t kMessageRetention = 30ULL * 24ULL * 60ULL * 60ULL;
constexpr int kRequestsPerHour = 20;
constexpr int kMessagesPerMinute = 20;

[[nodiscard]] bool recent_count(sqlite3* database,
                                const char* sql,
                                sqlite3_int64 accountId,
                                std::uint64_t since,
                                int& count) noexcept {
    count = 0;
    Statement statement(database, sql);
    sqlite3_stmt* query = statement.get();
    if (query == nullptr || sqlite3_bind_int64(query, 1, accountId) != SQLITE_OK
        || sqlite3_bind_int64(query, 2, static_cast<sqlite3_int64>(since)) != SQLITE_OK
        || sqlite3_step(query) != SQLITE_ROW) {
        return false;
    }
    count = sqlite3_column_int(query, 0);
    return true;
}

[[nodiscard]] bool within_request_budget(sqlite3* database,
                                         sqlite3_int64 accountId,
                                         std::uint64_t nowSeconds) noexcept {
    const std::uint64_t since = nowSeconds > 3600ULL ? nowSeconds - 3600ULL : 0;
    int count = 0;
    return recent_count(database,
                        "SELECT COUNT(*) FROM friendships WHERE requester_account_id=?1"
                        " AND created_at>=?2;",
                        accountId,
                        since,
                        count)
           && count < kRequestsPerHour;
}

[[nodiscard]] bool within_message_budget(sqlite3* database,
                                         sqlite3_int64 accountId,
                                         std::uint64_t nowSeconds) noexcept {
    const std::uint64_t since = nowSeconds > 60ULL ? nowSeconds - 60ULL : 0;
    int count = 0;
    return recent_count(database,
                        "SELECT COUNT(*) FROM direct_messages WHERE from_account_id=?1"
                        " AND sent_at>=?2;",
                        accountId,
                        since,
                        count)
           && count < kMessagesPerMinute;
}

[[nodiscard]] std::size_t sanitize_message(std::string_view body,
                                           std::span<char> output) noexcept {
    if (output.size() < 2) {
        return 0;
    }
    std::size_t written = 0;
    bool pendingSpace = false;
    for (const char character : body) {
        if (written + 1 >= output.size()) {
            break;
        }
        const auto byte = static_cast<unsigned char>(character);
        const bool space = byte <= 0x20U || byte == 0x7FU;
        if (space) {
            pendingSpace = written > 0;
            continue;
        }
        if (pendingSpace) {
            output[written++] = ' ';
            pendingSpace = false;
            if (written + 1 >= output.size()) {
                break;
            }
        }
        output[written++] = character;
    }
    output[written] = '\0';
    return written;
}

[[nodiscard]] bool apply_relationship(sqlite3* database,
                                      sqlite3_int64 actorAccountId,
                                      sqlite3_int64 targetAccountId,
                                      RelationshipAction action,
                                      std::uint64_t now,
                                      const char*& outcome) noexcept {
    bool success = false;
    outcome = nullptr;
    if (action == RelationshipAction::add) {
        sqlite3_int64 requester = 0;
        sqlite3_int64 recipient = 0;
        int relationState = 0;
        success = relation(database,
                           actorAccountId,
                           targetAccountId,
                           requester,
                           recipient,
                           relationState);
        if (success && relationState == 0) {
            Statement statement(
                database,
                "INSERT INTO friendships(requester_account_id,recipient_account_id,state,"
                "created_at,updated_at) VALUES(?1,?2,1,?3,?3);");
            sqlite3_stmt* insert = statement.get();
            success = insert != nullptr
                      && sqlite3_bind_int64(insert, 1, actorAccountId) == SQLITE_OK
                      && sqlite3_bind_int64(insert, 2, targetAccountId) == SQLITE_OK
                      && sqlite3_bind_int64(insert, 3, static_cast<sqlite3_int64>(now))
                             == SQLITE_OK
                      && sqlite3_step(insert) == SQLITE_DONE;
            outcome = "Friend request sent to %s.";
        } else if (success && relationState == 1 && requester == targetAccountId) {
            Statement statement(
                database,
                "UPDATE friendships SET state=2,updated_at=?1 WHERE"
                " requester_account_id=?2 AND recipient_account_id=?3 AND state=1;");
            sqlite3_stmt* update = statement.get();
            success = update != nullptr
                      && sqlite3_bind_int64(update, 1, static_cast<sqlite3_int64>(now))
                             == SQLITE_OK
                      && sqlite3_bind_int64(update, 2, targetAccountId) == SQLITE_OK
                      && sqlite3_bind_int64(update, 3, actorAccountId) == SQLITE_OK
                      && sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(database) == 1;
            outcome = "Friend request accepted; %s is now your friend.";
        } else if (success && relationState == 1) {
            outcome = "A request to %s is already pending.";
        } else if (success && relationState == 2) {
            outcome = "%s is already your friend.";
        } else {
            success = false;
        }
    } else if (action == RelationshipAction::accept) {
        Statement statement(
            database,
            "UPDATE friendships SET state=2,updated_at=?1 WHERE requester_account_id=?2"
            " AND recipient_account_id=?3 AND state=1;");
        sqlite3_stmt* update = statement.get();
        success = update != nullptr
                  && sqlite3_bind_int64(update, 1, static_cast<sqlite3_int64>(now))
                         == SQLITE_OK
                  && sqlite3_bind_int64(update, 2, targetAccountId) == SQLITE_OK
                  && sqlite3_bind_int64(update, 3, actorAccountId) == SQLITE_OK
                  && sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(database) == 1;
        outcome = "Accepted %s's friend request.";
    } else if (action == RelationshipAction::decline) {
        Statement statement(
            database,
            "DELETE FROM friendships WHERE requester_account_id=?1"
            " AND recipient_account_id=?2 AND state=1;");
        sqlite3_stmt* remove = statement.get();
        success = remove != nullptr
                  && sqlite3_bind_int64(remove, 1, targetAccountId) == SQLITE_OK
                  && sqlite3_bind_int64(remove, 2, actorAccountId) == SQLITE_OK
                  && sqlite3_step(remove) == SQLITE_DONE && sqlite3_changes(database) == 1;
        outcome = "Declined %s's friend request.";
    } else if (action == RelationshipAction::remove) {
        Statement statement(
            database,
            "DELETE FROM friendships WHERE (requester_account_id=?1"
            " AND recipient_account_id=?2) OR (requester_account_id=?2"
            " AND recipient_account_id=?1);");
        sqlite3_stmt* remove = statement.get();
        success = remove != nullptr
                  && sqlite3_bind_int64(remove, 1, actorAccountId) == SQLITE_OK
                  && sqlite3_bind_int64(remove, 2, targetAccountId) == SQLITE_OK
                  && sqlite3_step(remove) == SQLITE_DONE && sqlite3_changes(database) == 1;
        outcome = "Removed relationship with %s.";
    }
    return success;
}

[[nodiscard]] RelationshipAction relationship_of(DiscordAction action) noexcept {
    switch (action) {
    case DiscordAction::friendAccept:
        return RelationshipAction::accept;
    case DiscordAction::friendDecline:
        return RelationshipAction::decline;
    case DiscordAction::friendRemove:
        return RelationshipAction::remove;
    default:
        return RelationshipAction::add;
    }
}

[[nodiscard]] bool rename_account_locked(sqlite3_int64 accountId, std::string_view name,
                                         std::uint64_t nowSeconds, std::span<char> message,
                                         std::size_t& written) noexcept {
    if (!valid_account_name(name)) {
        (void)format_message(message, written, "Names must be 3-63 letters, numbers, spaces, _ or -.");
        return false;
    }
    if (!execute(g_database, "BEGIN IMMEDIATE;")) {
        (void)format_message(message, written, "The server is busy. Try again.");
        return false;
    }
    Statement statement(g_database,
        "UPDATE accounts SET display_name=?1,updated_at=?2 WHERE id=?3;");
    auto* update = statement.get();
    const bool success = update && bind_text(update, 1, name)
        && sqlite3_bind_int64(update, 2, static_cast<sqlite3_int64>(nowSeconds)) == SQLITE_OK
        && sqlite3_bind_int64(update, 3, accountId) == SQLITE_OK
        && sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(g_database) == 1
        && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
        (void)format_message(message, written, "That account name is unavailable.");
        return false;
    }
    return format_message(message, written,
        "Account renamed to %.*s. Restart Dawn if the old name is still shown.",
        static_cast<int>(name.size()), name.data());
}

} // namespace

bool initialize(void* module,
                const dawn::state::AccountState& seed,
                std::string_view displayName,
                std::uint64_t platformId,
                LoadResult& result) noexcept {
    result = {};
    if (module == nullptr || displayName.empty() || platformId == 0
        || !dawn::state::account::valid(seed) || seed.primarySoid == 0) {
        report("initialize", "invalid");
        return false;
    }
    dawn::core::path::Buffer path;
    if (!make_database_path(module, path)) {
        report("path", "fail");
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (g_database != nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        report("initialize", "duplicate");
        return false;
    }
    sqlite3* database = nullptr;
    bool success = sqlite3_open16(path.chars.data(), &database) == SQLITE_OK
                   && database != nullptr
                   && sqlite3_busy_timeout(database, kBusyTimeoutMilliseconds) == SQLITE_OK
                   && migrate(database);
    sqlite3_int64 accountId = 0;
    std::uint64_t revision = 0;
    dawn::state::AccountState loaded{};
#if defined(RESTORATION_DEDICATED_SERVER) && RESTORATION_DEDICATED_SERVER
    LoadStatus status = success
                            ? load_account_by_soid(
                                  database, seed.primarySoid, accountId, revision, loaded)
                            : LoadStatus::failed;
#else
    LoadStatus status = success ? load_account(database, platformId, accountId, revision, loaded)
                                : LoadStatus::failed;
#endif
    bool seeded = false;
    if (status == LoadStatus::missing) {
        success = seed_account(database, seed, displayName, platformId, accountId);
        status = success ? load_account(database, platformId, accountId, revision, loaded)
                         : LoadStatus::failed;
        seeded = success && status == LoadStatus::loaded;
    }
    success = success && status == LoadStatus::loaded
              && loaded.primarySoid == seed.primarySoid && account_codec::self_test(loaded);
    dawn::state::social::Snapshot social{};
    success = success
              && initialize_social(database, accountId, platformId, social);
    std::uint64_t activityRevision = 0;
    dawn::state::activity::ActivityState loadedActivity{};
    const LoadStatus activityStatus =
        success ? load_activity(database,
                                accountId,
                                loaded.primarySoid,
                                activityRevision,
                                loadedActivity)
                : LoadStatus::failed;
    success = success
              && (activityStatus == LoadStatus::missing
                  || activityStatus == LoadStatus::loaded);
    if (success) {
        Statement namespaceStatement(
            database,
            "INSERT OR IGNORE INTO account_namespaces(account_id,namespace) VALUES(?1,1);");
        sqlite3_stmt* insertNamespace = namespaceStatement.get();
        success = insertNamespace != nullptr
                  && sqlite3_bind_int64(insertNamespace, 1, accountId) == SQLITE_OK
                  && sqlite3_step(insertNamespace) == SQLITE_DONE;
    }
    if (!success) {
        if (database != nullptr) {
            (void)sqlite3_close_v2(database);
        }
        ReleaseSRWLockExclusive(&g_lock);
        report("initialize", "fail");
        return false;
    }
    g_database = database;
    g_accountId = accountId;
    g_revision = revision;
    g_activityRevision = activityRevision;
    g_starterSeed = seed;
    result.account = loaded;
    result.activity = loadedActivity;
    result.social = social;
    result.revision = revision;
    result.activityRevision = activityRevision;
    result.seeded = seeded;
    result.activityRestored = activityStatus == LoadStatus::loaded;
    ReleaseSRWLockExclusive(&g_lock);
    report(seeded ? "seed" : "load", "ok", revision);
    return true;
}

bool save(const dawn::state::AccountState& account) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool success = save_account_locked(g_accountId, g_revision, account);
    const std::uint64_t revision = g_revision;
    ReleaseSRWLockExclusive(&g_lock);
    report("commit", success ? "ok" : "fail", revision);
    return success;
}

static bool acquire_weapon_locked_dispatch(std::uint64_t platformId, const WeaponAcquireRequest& request,
                    WeaponAcquireResult& result, std::string_view& error) noexcept {
    namespace data = dawn::state::build_data;
    namespace inventory = dawn::state::account::inventory;
    result = {}; error = "Acquire refused.";
    if (!platformId || !request.requestId || request.plugCount > inventory::kPlugCapacity) return false;
    std::string key = std::to_string(request.characterSoid) + " " + std::to_string(request.definitionHash)
        + " " + std::to_string(request.plugCount);
    for (std::size_t lane = 0; lane < request.plugCount; ++lane) key += " " + std::to_string(request.plugs[lane]);
    AcquireSRWLockExclusive(&g_lock);
    static inventory::weapon_roll::Catalog rollCatalog;
    static bool rollCatalogReady = false;
    if (!rollCatalogReady) rollCatalogReady = rollCatalog.build();
    if (!rollCatalogReady) {
        ReleaseSRWLockExclusive(&g_lock);
        error = "Weapon catalog is unavailable.";
        return false;
    }
    bool success = false, changed = false;
    std::uint64_t changedAccount = 0;
    do {
        sqlite3_int64 accountId = 0;
        std::array<char, kAccountNameCapacity> name{};
        if (!g_database || !account_for_platform(g_database, platformId, accountId, name)) break;
        Statement prior(g_database, "SELECT request_text,instance_soid,character_soid,definition_hash,revision FROM weapon_acquire_receipts WHERE account_id=?1 AND request_id=?2;");
        auto* query = prior.get();
        if (!query || sqlite3_bind_int64(query, 1, accountId) != SQLITE_OK || !bind_u64_blob(query, 2, request.requestId)) break;
        const int priorStatus = sqlite3_step(query);
        if (priorStatus != SQLITE_ROW && priorStatus != SQLITE_DONE) break;
        if (priorStatus == SQLITE_ROW) {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
            if (!text || key != text) { error = "Request ID already used for another roll."; break; }
            success = read_u64_blob(query, 1, result.instanceSoid) && read_u64_blob(query, 2, result.characterSoid);
            result.definitionHash = static_cast<std::uint32_t>(sqlite3_column_int64(query, 3));
            result.revision = static_cast<std::uint64_t>(sqlite3_column_int64(query, 4));
            break;
        }
        dawn::state::AccountState account{};
        std::uint64_t revision = 0;
        if (load_account_by_id(g_database, accountId, revision, account) != LoadStatus::loaded) break;
        prefer_live_account(accountId, account, revision);
        std::size_t index = account.characterCount;
        for (std::size_t i = 0; i < account.characterCount; ++i)
            if (account.characters[i].selected && (!request.characterSoid || account.characters[i].soid == request.characterSoid)) { index = i; break; }
        if (index == account.characterCount) { error = "Select the current character before acquiring."; break; }
        auto& character = account.characters[index];
        data::items::Definition definition{};
        data::items::details::Definition detail{};
        if (!data::find_item_definition_hash(request.definitionHash, definition)
            || !data::find_configured_item_detail(definition.definitionIndex, detail)
            || !inventory::weapon_roll::is_weapon(detail)
            || detail.ordinarySocketCount != request.plugCount) { error = "Unsupported weapon or socket count."; break; }
        bool valid = true;
        for (std::size_t lane = 0; lane < request.plugCount; ++lane) {
            data::items::Definition plug{};
            if (!request.plugs[lane]) {
                valid = valid && detail.initialPlugIndices[lane] == data::items::details::kUnavailableItemIndex;
            } else valid = valid && data::find_item_definition_hash(request.plugs[lane], plug)
                && rollCatalog.allowed(definition.definitionIndex, lane, plug.definitionIndex);
        }
        if (!valid) { error = "A perk is incompatible with its weapon socket."; break; }
        if (character.inventory.count >= character.inventory.values.size()
            || character.nextInventorySerial >= static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) {
            error = "Character inventory is full."; break;
        }
        inventory::Item item{};
        for (unsigned attempt = 0; attempt < 128 && !item.instanceSoid; ++attempt) {
            std::uint64_t random = 0;
            if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&random), sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) break;
            const auto candidate = inventory::kWeaponPickerInstancePrefix | (random & ~inventory::kWeaponPickerInstancePrefixMask);
            if (!identity_in_use(account, character, candidate)) item.instanceSoid = candidate;
        }
        if (!item.instanceSoid) break;
        item.definitionHash = request.definitionHash; item.quantity = 1;
        for (const auto& equipped : character.equipment.slots) if (equipped) item.level = (std::max)(item.level, equipped->level);
        item.mutationSerial = static_cast<std::int32_t>(character.nextInventorySerial++);
        item.sockets.policy = inventory::SocketPolicy::authored; item.sockets.plugCount = request.plugCount;
        for (std::size_t lane = 0; lane < request.plugCount; ++lane) if (request.plugs[lane]) item.sockets.plugs[lane] = request.plugs[lane];
        character.inventory.values[character.inventory.count++] = item;
        dawn::middleware::datagen::family4::loadout::ResolvedLoadout resolved{};
        if (!dawn::state::account::valid(account)
            || !dawn::middleware::datagen::family4::loadout::resolve(account, index, resolved)) {
            error = "Weapon bucket is full or the roll cannot be resolved."; break;
        }
        result.characterSoid = character.soid; result.instanceSoid = item.instanceSoid; result.definitionHash = item.definitionHash;
        if (!save_account_locked(accountId, revision, account, &request, &result, key)) { error = "Unable to persist weapon."; result = {}; break; }
        result.revision = revision;
        for (auto& record : g_runtimeRecords) if (record && record->accountId == accountId) { record->account = account; record->revision = revision; }
        changedAccount = account.primarySoid; success = changed = true;
    } while (false);
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) dawn::server::bap::account_inventory_acquired(changedAccount, result.characterSoid, result.instanceSoid);
    return success;
}

bool acquire_weapon(std::uint64_t platformId, const WeaponAcquireRequest& request,
                    WeaponAcquireResult& result, std::string_view& error) noexcept {
    struct Context {
        std::uint64_t platformId;
        const WeaponAcquireRequest* request;
        WeaponAcquireResult* result;
        std::string_view* error;
    } context{platformId, &request, &result, &error};
    return dawn::server::bap::with_account_transaction([](void* opaque) noexcept {
        const auto& value = *static_cast<Context*>(opaque);
        return acquire_weapon_locked_dispatch(value.platformId, *value.request, *value.result, *value.error);
    }, &context);
}

bool delete_character(std::uint64_t characterSoid) noexcept {
    if (characterSoid == 0) {
        return false;
    }
    std::uint64_t changedAccount = 0;
    AcquireSRWLockExclusive(&g_lock);
    // a mark on a deleted character would turn the next create into a restyle of nothing
    for (CustomizeTarget& target : g_customizeTargets) {
        if (target.characterSoid == characterSoid) {
            target = {};
        }
    }
    bool success = false;
    if (g_database != nullptr) {
        bool targeted = false;
        for (auto& record : g_runtimeRecords) {
            if (record == nullptr) {
                continue;
            }
            targeted = true;
            // use the live image, not a reload: loaders clear the selection and would drop the current pick
            dawn::state::AccountState account = record->account;
            std::uint64_t revision = record->revision;
            if (!erase_character(account, characterSoid)) {
                continue;
            }
            if (dawn::state::account::valid(account)
                && save_account_locked(record->accountId, revision, account)) {
                record->account = account;
                record->revision = revision;
                changedAccount = account.primarySoid;
                success = true;
            }
        }
        if (!targeted && g_accountId > 0) {
            dawn::state::AccountState account{};
            std::uint64_t revision = 0;
            if (load_account_by_id(g_database, g_accountId, revision, account) == LoadStatus::loaded
                && erase_character(account, characterSoid)
                && dawn::state::account::valid(account)
                && save_account_locked(g_accountId, revision, account)) {
                changedAccount = account.primarySoid;
                success = true;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    report("character_delete", success ? "ok" : "fail", g_revision);
    if (success) {
        dawn::server::bap::account_roster_changed(changedAccount);
    }
    return success;
}

[[nodiscard]] bool character_delete_callback([[maybe_unused]] void* context,
                                             std::uint64_t characterSoid) noexcept {
    return delete_character(characterSoid);
}

[[nodiscard]] bool character_create_callback([[maybe_unused]] void* context,
                                             const dawn::state::CharacterCreateRequest& request,
                                             std::uint64_t& createdSoid) noexcept {
    // qualified: ADL also finds dawn::state::create_character
    return ::restoration::account_store::create_character(request, createdSoid);
}

[[nodiscard]] bool customize_pending_callback([[maybe_unused]] void* context,
                                              std::uint64_t accountSoid) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool pending = live_customize_target(accountSoid) != nullptr;
    ReleaseSRWLockExclusive(&g_lock);
    return pending;
}

void install_state_hooks() noexcept {
    (void)dawn::state::register_character_delete_handler(nullptr, &character_delete_callback);
    (void)dawn::state::register_character_create_handler(nullptr, &character_create_callback);
    (void)dawn::state::register_customize_pending_query(nullptr, &customize_pending_callback);
}

bool create_character(std::uint8_t characterClass,
                      std::uint8_t race,
                      std::uint8_t gender) noexcept {
    dawn::state::CharacterCreateRequest request{};
    request.characterClass = characterClass;
    request.race = race;
    request.gender = gender;
    std::uint64_t createdSoid = 0;
    return ::restoration::account_store::create_character(request, createdSoid);
}

bool create_character(const dawn::state::CharacterCreateRequest& request,
                      std::uint64_t& createdSoid) noexcept {
    createdSoid = 0;
    std::uint64_t changedAccount = 0;
    bool customized = false;
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr) {
        RuntimeRecord* target = nullptr;
        for (auto& record : g_runtimeRecords) {
            if (record != nullptr
                && (request.accountSoid == 0 || record->account.primarySoid == request.accountSoid)) {
                target = record.get();
                break;
            }
        }
        if (target != nullptr) {
            dawn::state::AccountState account = target->account;
            std::uint64_t revision = target->revision;
            std::uint64_t soid = 0;
            CustomizeTarget* marked =
                request.accountSoid != 0 ? live_customize_target(request.accountSoid) : nullptr;
            if (marked != nullptr) {
                bool owned = false;
                for (std::size_t index = 0; index < account.characterCount; ++index) {
                    owned = owned || account.characters[index].soid == marked->characterSoid;
                }
                if (!owned) {
                    *marked = {};
                    marked = nullptr;
                }
            }
            customized = marked != nullptr;
            const bool built = customized
                                   ? apply_customization(account, request, marked->characterSoid, soid)
                                   : append_character(account, request, soid);
            if (built && dawn::state::account::valid(account)
                && save_account_locked(target->accountId, revision, account)) {
                if (marked != nullptr) {
                    *marked = {};
                }
                const sqlite3_int64 accountId = target->accountId;
                for (auto& record : g_runtimeRecords) {
                    if (record != nullptr && record->accountId == accountId) {
                        record->account = account;
                        record->revision = revision;
                    }
                }
                changedAccount = account.primarySoid;
                createdSoid = soid;
                success = true;
            }
        } else if (request.accountSoid == 0 && g_accountId > 0) {
            dawn::state::AccountState account{};
            std::uint64_t revision = 0;
            std::uint64_t soid = 0;
            if (load_account_by_id(g_database, g_accountId, revision, account) == LoadStatus::loaded
                && append_character(account, request, soid)
                && dawn::state::account::valid(account)
                && save_account_locked(g_accountId, revision, account)) {
                changedAccount = account.primarySoid;
                createdSoid = soid;
                success = true;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    report(customized ? "character_customize" : "character_create",
           success ? "ok" : "fail",
           g_revision);
    if (success) {
        dawn::server::bap::account_roster_changed(changedAccount);
    }
    return success;
}

bool list_characters(std::span<char> output, std::size_t& written) noexcept {
    written = 0;
    if (output.empty()) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    dawn::state::AccountState account{};
    std::uint64_t revision = g_revision;
    const bool loaded =
        g_database != nullptr && g_accountId > 0
        && load_account_by_id(g_database, g_accountId, revision, account) == LoadStatus::loaded;
    if (loaded) {
        for (std::size_t index = 0; index < account.characterCount; ++index) {
            const dawn::state::CharacterState& character = account.characters[index];
            const int count = std::snprintf(
                output.data() + written,
                output.size() - written,
                "%zu %llu %u %u %u %u\n",
                index,
                static_cast<unsigned long long>(character.soid),
                static_cast<unsigned>(static_cast<std::uint8_t>(character.characterClass)),
                static_cast<unsigned>(static_cast<std::uint8_t>(character.race)),
                static_cast<unsigned>(static_cast<std::uint8_t>(character.gender)),
                character.selected ? 1U : 0U);
            if (count <= 0 || static_cast<std::size_t>(count) >= output.size() - written) {
                written = 0;
                break;
            }
            written += static_cast<std::size_t>(count);
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return written != 0;
}

bool own_characters(std::uint64_t platformId, std::span<char> output, std::size_t& written) noexcept {
    written = 0;
    if (platformId == 0 || output.empty()) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    dawn::state::AccountState account{};
    const bool loaded = platform_account_locked(platformId, account);
    const CustomizeTarget* const marked =
        loaded ? live_customize_target(account.primarySoid) : nullptr;
    const std::uint64_t selected = loaded ? dawn::state::account::selected_character_soid(account) : 0;
    const std::uint64_t presented =
        selected != 0 ? selected : (loaded ? dawn::state::account::banner_character_soid(account) : 0);
    bool fits = true;
    for (std::size_t index = 0; loaded && fits && index < account.characterCount; ++index) {
        const dawn::state::CharacterState& character = account.characters[index];
        std::int32_t power = 0;
        if (!dawn::state::equipment::light::resolution::character_light(account, index, power) || power < 0) {
            power = 0;
        }
        const int count = std::snprintf(
            output.data() + written,
            output.size() - written,
            "%llu %u %u %u %u %d %u\n",
            static_cast<unsigned long long>(character.soid),
            static_cast<unsigned>(static_cast<std::uint8_t>(character.characterClass)),
            static_cast<unsigned>(static_cast<std::uint8_t>(character.race)),
            static_cast<unsigned>(static_cast<std::uint8_t>(character.gender)),
            marked != nullptr && marked->characterSoid == character.soid ? 1U : 0U,
            static_cast<int>(power),
            character.soid == presented ? 1U : 0U);
        fits = count > 0 && static_cast<std::size_t>(count) < output.size() - written;
        written += fits ? static_cast<std::size_t>(count) : 0;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!fits) {
        written = 0;
    }
    return loaded && fits;
}

bool mark_customize(std::uint64_t platformId, std::uint64_t characterSoid) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    dawn::state::AccountState account{};
    bool success = platform_account_locked(platformId, account) && account.primarySoid != 0;
    CustomizeTarget* target = success ? live_customize_target(account.primarySoid) : nullptr;
    if (success && characterSoid == 0) {
        if (target != nullptr) {
            *target = {};
        }
    } else if (success) {
        bool owned = false;
        for (std::size_t index = 0; index < account.characterCount; ++index) {
            owned = owned || account.characters[index].soid == characterSoid;
        }
        if (owned && target == nullptr) {
            target = &g_customizeTargets.front();
            for (CustomizeTarget& candidate : g_customizeTargets) {
                if (candidate.accountSoid == 0) {
                    target = &candidate;
                    break;
                }
                if (candidate.expiresAtTick < target->expiresAtTick) {
                    target = &candidate;
                }
            }
        }
        success = owned;
        if (owned) {
            target->accountSoid = account.primarySoid;
            target->characterSoid = characterSoid;
            target->expiresAtTick = GetTickCount64() + kCustomizeLifetimeMilliseconds;
        }
    }
    const std::uint64_t revision = g_revision;
    ReleaseSRWLockExclusive(&g_lock);
    report(characterSoid == 0 ? "character_customize_clear" : "character_customize_mark",
           success ? "ok" : "fail",
           revision);
    return success;
}

bool save_callback([[maybe_unused]] void* context,
                   const dawn::state::AccountState& account) noexcept {
    return save(account);
}

bool save_activity(const dawn::state::activity::ActivityState& activity) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool success =
        save_activity_for_account_locked(g_accountId, g_activityRevision, activity);
    const std::uint64_t revision = g_activityRevision;
    ReleaseSRWLockExclusive(&g_lock);
    report("world_commit", success ? "ok" : "fail", revision);
    return success;
}

bool save_activity_callback(
    [[maybe_unused]] void* context,
    const dawn::state::activity::ActivityState& activity) noexcept {
    return save_activity(activity);
}

bool issue_discord_link_code(std::uint64_t platformId,
                             std::uint64_t nowSeconds,
                             DiscordLinkCode& result) noexcept {
    result = {};
    if (platformId == 0 || nowSeconds == 0
        || nowSeconds > static_cast<std::uint64_t>((std::numeric_limits<sqlite3_int64>::max)())
                               - kDiscordLinkLifetimeSeconds) {
        return false;
    }
    std::array<char, kCanonicalLinkCodeSize> canonical{};
    std::array<char, kDiscordLinkCodeCapacity> display{};
    std::array<std::byte, kDigestSize> digest{};
    if (!generate_link_code(canonical, display)
        || !sha256(std::as_bytes(std::span(canonical)), digest)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 accountId = 0;
    std::array<char, kAccountNameCapacity> name{};
    bool success = g_database != nullptr
                   && account_for_platform(g_database, platformId, accountId, name)
                   && execute(g_database, "BEGIN IMMEDIATE;");
    if (success) {
        Statement statement(
            g_database,
            "INSERT INTO discord_link_codes(account_id,code_hash,expires_at,created_at)"
            " VALUES(?1,?2,?3,?4) ON CONFLICT(account_id) DO UPDATE SET"
            " code_hash=excluded.code_hash,expires_at=excluded.expires_at,"
            " created_at=excluded.created_at;");
        sqlite3_stmt* upsert = statement.get();
        success = upsert != nullptr
                  && sqlite3_bind_int64(upsert, 1, accountId) == SQLITE_OK
                  && bind_bytes(upsert, 2, digest)
                  && sqlite3_bind_int64(
                         upsert,
                         3,
                         static_cast<sqlite3_int64>(nowSeconds + kDiscordLinkLifetimeSeconds))
                         == SQLITE_OK
                  && sqlite3_bind_int64(upsert, 4, static_cast<sqlite3_int64>(nowSeconds))
                         == SQLITE_OK
                  && sqlite3_step(upsert) == SQLITE_DONE
                  && execute(g_database, "COMMIT;");
    }
    if (!success && g_database != nullptr) {
        (void)execute(g_database, "ROLLBACK;");
    }
    if (success) {
        result.code = display;
        result.accountName = name;
        result.expiresAt = nowSeconds + kDiscordLinkLifetimeSeconds;
    }
    ReleaseSRWLockExclusive(&g_lock);
    SecureZeroMemory(canonical.data(), canonical.size());
    SecureZeroMemory(digest.data(), digest.size());
    return success;
}

bool redeem_discord_link(std::string_view discordUserId,
                         std::string_view code,
                         std::uint64_t nowSeconds,
                         std::span<char> message,
                         std::size_t& written) noexcept {
    written = 0;
    std::array<char, kCanonicalLinkCodeSize> canonical{};
    std::array<std::byte, kDigestSize> digest{};
    if (!valid_discord_user_id(discordUserId) || nowSeconds == 0
        || !canonical_link_code(code, canonical)
        || !sha256(std::as_bytes(std::span(canonical)), digest)) {
        (void)format_message(message, written, "Invalid or expired link code.");
        return false;
    }

    AcquireSRWLockExclusive(&g_lock);
    bool success = g_database != nullptr && execute(g_database, "BEGIN IMMEDIATE;");
    sqlite3_int64 accountId = 0;
    std::array<char, kAccountNameCapacity> accountName{};
    if (success) {
        Statement statement(
            g_database,
            "SELECT c.account_id,a.display_name FROM discord_link_codes c"
            " JOIN accounts a ON a.id=c.account_id"
            " WHERE c.code_hash=?1 AND c.expires_at>=?2;");
        sqlite3_stmt* query = statement.get();
        success = bind_bytes(query, 1, digest)
                  && sqlite3_bind_int64(query, 2, static_cast<sqlite3_int64>(nowSeconds))
                         == SQLITE_OK
                  && sqlite3_step(query) == SQLITE_ROW
                  && copy_column_text(query, 1, accountName);
        if (success) {
            accountId = sqlite3_column_int64(query, 0);
            success = accountId > 0 && sqlite3_step(query) == SQLITE_DONE;
        }
    }
    if (success) {
        Statement removeExisting(
            g_database,
            "DELETE FROM discord_links WHERE discord_user_id=?1 OR account_id=?2;");
        sqlite3_stmt* remove = removeExisting.get();
        success = bind_text(remove, 1, discordUserId)
                  && sqlite3_bind_int64(remove, 2, accountId) == SQLITE_OK
                  && sqlite3_step(remove) == SQLITE_DONE;
    }
    if (success) {
        Statement insertLink(
            g_database,
            "INSERT INTO discord_links(discord_user_id,account_id,linked_at,updated_at)"
            " VALUES(?1,?2,?3,?3);");
        sqlite3_stmt* insert = insertLink.get();
        success = bind_text(insert, 1, discordUserId)
                  && sqlite3_bind_int64(insert, 2, accountId) == SQLITE_OK
                  && sqlite3_bind_int64(insert, 3, static_cast<sqlite3_int64>(nowSeconds))
                         == SQLITE_OK
                  && sqlite3_step(insert) == SQLITE_DONE;
    }
    if (success) {
        Statement consumeCode(g_database,
                              "DELETE FROM discord_link_codes WHERE account_id=?1;");
        sqlite3_stmt* remove = consumeCode.get();
        success = remove != nullptr && sqlite3_bind_int64(remove, 1, accountId) == SQLITE_OK
                  && sqlite3_step(remove) == SQLITE_DONE && sqlite3_changes(g_database) == 1;
    }
    success = success && execute(g_database, "COMMIT;");
    if (!success && g_database != nullptr) {
        (void)execute(g_database, "ROLLBACK;");
    }
    ReleaseSRWLockExclusive(&g_lock);
    SecureZeroMemory(canonical.data(), canonical.size());
    SecureZeroMemory(digest.data(), digest.size());
    if (!success) {
        (void)format_message(message, written, "Invalid or expired link code.");
        return false;
    }
    return format_message(message,
                          written,
                          "Discord linked to %s.",
                          accountName.data());
}

bool discord_link_state(std::uint64_t platformId, DiscordLinkState& output) noexcept {
    output = {};
    if (platformId == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr) {
        sqlite3_int64 accountId = 0;
        std::array<char, kAccountNameCapacity> name{};
        if (account_for_platform(g_database, platformId, accountId, name)) {
            output.accountName = name;
            Statement statement(g_database,
                                "SELECT 1 FROM discord_links WHERE account_id=?1;");
            sqlite3_stmt* query = statement.get();
            success = query != nullptr
                      && sqlite3_bind_int64(query, 1, accountId) == SQLITE_OK;
            if (success) {
                const int step = sqlite3_step(query);
                if (step == SQLITE_ROW) {
                    output.linked = true;
                } else if (step != SQLITE_DONE) {
                    success = false;
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

bool request_discord_unlink(std::uint64_t platformId,
                            std::uint64_t nowSeconds,
                            DiscordLinkState& output) noexcept {
    output = {};
    if (platformId == 0 || nowSeconds == 0
        || nowSeconds > static_cast<std::uint64_t>((std::numeric_limits<sqlite3_int64>::max)())
                               - kDiscordUnlinkLifetimeSeconds) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr) {
        sqlite3_int64 accountId = 0;
        std::array<char, kAccountNameCapacity> name{};
        success = account_for_platform(g_database, platformId, accountId, name);
        if (success) {
            output.accountName = name;
            Statement lookup(g_database,
                             "SELECT discord_user_id FROM discord_links WHERE account_id=?1;");
            sqlite3_stmt* query = lookup.get();
            std::array<char, 21> discordUserId{};
            success = query != nullptr
                      && sqlite3_bind_int64(query, 1, accountId) == SQLITE_OK
                      && sqlite3_step(query) == SQLITE_ROW
                      && copy_column_text(query, 0, discordUserId)
                      && sqlite3_step(query) == SQLITE_DONE;
            if (success) {
                output.linked = true;
                Statement upsert(
                    g_database,
                    "INSERT INTO discord_unlink_requests(account_id,discord_user_id,created_at,"
                    "expires_at,notified_at) VALUES(?1,?2,?3,?4,NULL)"
                    " ON CONFLICT(account_id) DO UPDATE SET"
                    " discord_user_id=excluded.discord_user_id,created_at=excluded.created_at,"
                    " expires_at=excluded.expires_at,notified_at=NULL;");
                sqlite3_stmt* write = upsert.get();
                success = write != nullptr
                          && sqlite3_bind_int64(write, 1, accountId) == SQLITE_OK
                          && bind_text(write, 2, std::string_view(discordUserId.data()))
                          && sqlite3_bind_int64(
                                 write, 3, static_cast<sqlite3_int64>(nowSeconds))
                                 == SQLITE_OK
                          && sqlite3_bind_int64(
                                 write,
                                 4,
                                 static_cast<sqlite3_int64>(nowSeconds
                                                            + kDiscordUnlinkLifetimeSeconds))
                                 == SQLITE_OK
                          && sqlite3_step(write) == SQLITE_DONE;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

bool claim_discord_unlinks(std::span<PendingUnlink> output, std::size_t& count) noexcept {
    count = 0;
    if (output.empty()) {
        return false;
    }
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const std::uint64_t now = value > 0 ? static_cast<std::uint64_t>(value) : 0;
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr && now != 0) {
        Statement prune(g_database,
                        "DELETE FROM discord_unlink_requests WHERE expires_at<?1;");
        sqlite3_stmt* expired = prune.get();
        (void)(expired != nullptr
                   && sqlite3_bind_int64(expired, 1, static_cast<sqlite3_int64>(now))
                          == SQLITE_OK
                   && sqlite3_step(expired) == SQLITE_DONE);

        Statement select(g_database,
                         "SELECT r.discord_user_id,a.display_name FROM discord_unlink_requests r"
                         " JOIN accounts a ON a.id=r.account_id"
                         " WHERE r.notified_at IS NULL AND r.expires_at>=?1"
                         " ORDER BY r.created_at LIMIT ?2;");
        sqlite3_stmt* query = select.get();
        success = query != nullptr
                  && sqlite3_bind_int64(query, 1, static_cast<sqlite3_int64>(now)) == SQLITE_OK
                  && sqlite3_bind_int(query, 2, static_cast<int>(output.size())) == SQLITE_OK;
        while (success && count < output.size()) {
            const int step = sqlite3_step(query);
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW
                || !copy_column_text(query, 0, output[count].discordUserId)
                || !copy_column_text(query, 1, output[count].accountName)) {
                success = false;
                break;
            }
            ++count;
        }
        if (success && count != 0) {
            Statement mark(g_database,
                           "UPDATE discord_unlink_requests SET notified_at=?1"
                           " WHERE notified_at IS NULL;");
            sqlite3_stmt* write = mark.get();
            success = write != nullptr
                      && sqlite3_bind_int64(write, 1, static_cast<sqlite3_int64>(now))
                             == SQLITE_OK
                      && sqlite3_step(write) == SQLITE_DONE;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!success) {
        count = 0;
    }
    return success;
}

bool execute_discord_unlink(std::string_view discordUserId,
                            bool confirmed,
                            std::uint64_t nowSeconds,
                            std::span<char> message,
                            std::size_t& written) noexcept {
    written = 0;
    if (!valid_discord_user_id(discordUserId) || nowSeconds == 0) {
        (void)format_message(message, written, "Link your account first with /account link.");
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 accountId = 0;
    std::array<char, kAccountNameCapacity> accountName{};
    const bool linked = g_database != nullptr
                        && account_for_discord(
                            g_database, discordUserId, accountId, accountName);
    if (!linked) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "That Discord account is no longer linked.");
        return false;
    }
    bool success = execute(g_database, "BEGIN IMMEDIATE;");
    if (success && confirmed) {
        Statement removeLink(g_database,
                             "DELETE FROM discord_links WHERE discord_user_id=?1;");
        sqlite3_stmt* link = removeLink.get();
        success = bind_text(link, 1, discordUserId) && sqlite3_step(link) == SQLITE_DONE
                  && sqlite3_changes(g_database) == 1;
    }
    if (success) {
        Statement removePending(g_database,
                                "DELETE FROM discord_unlink_requests WHERE account_id=?1;");
        sqlite3_stmt* pending = removePending.get();
        success = pending != nullptr && sqlite3_bind_int64(pending, 1, accountId) == SQLITE_OK
                  && sqlite3_step(pending) == SQLITE_DONE;
    }
    success = success && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!success) {
        (void)format_message(message, written, "The unlink could not be completed.");
        return false;
    }
    if (confirmed) {
        return format_message(message,
                              written,
                              "Discord unlinked from %s. You can link again from the game.",
                              accountName.data());
    }
    return format_message(message, written, "Unlink cancelled.");
}

bool rename_account(std::uint64_t platformId, std::string_view name,
                    std::uint64_t nowSeconds, std::span<char> message,
                    std::size_t& written) noexcept {
    written = 0;
    if (!platformId || !nowSeconds) {
        (void)format_message(message, written, "Sign in to Dawn first.");
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 accountId{};
    std::array<char, kAccountNameCapacity> oldName{};
    if (!g_database || !account_for_platform(g_database, platformId, accountId, oldName)) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "Play Dawn once on this PC first.");
        return false;
    }
    const bool success = rename_account_locked(accountId, name, nowSeconds, message, written);
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

bool execute_discord_action(std::string_view discordUserId,
                            DiscordAction action,
                            std::string_view argument,
                            std::uint64_t nowSeconds,
                            std::span<char> message,
                            std::size_t& written) noexcept {
    written = 0;
    if (!valid_discord_user_id(discordUserId) || nowSeconds == 0) {
        (void)format_message(message, written, "Link your account first with /account link.");
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 localAccountId = 0;
    std::array<char, kAccountNameCapacity> localName{};
    bool linked = g_database != nullptr
                  && account_for_discord(
                      g_database, discordUserId, localAccountId, localName);
    if (!linked) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "Link your account first with /account link.");
        return false;
    }

    if (action == DiscordAction::friendList) {
        Statement statement(
            g_database,
            "SELECT a.display_name,CASE WHEN f.state=2 THEN 'friend'"
            " WHEN f.requester_account_id=?1 THEN 'outgoing' ELSE 'incoming' END"
            " FROM friendships f JOIN accounts a ON a.id=CASE"
            " WHEN f.requester_account_id=?1 THEN f.recipient_account_id"
            " ELSE f.requester_account_id END"
            " WHERE f.requester_account_id=?1 OR f.recipient_account_id=?1"
            " ORDER BY f.state DESC,a.display_name;");
        sqlite3_stmt* query = statement.get();
        bool success = query != nullptr
                       && sqlite3_bind_int64(query, 1, localAccountId) == SQLITE_OK;
        written = 0;
        if (message.empty()) {
            ReleaseSRWLockExclusive(&g_lock);
            return false;
        }
        while (success) {
            const int step = sqlite3_step(query);
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW) {
                success = false;
                break;
            }
            const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(query, 0));
            const auto* relationText = reinterpret_cast<const char*>(sqlite3_column_text(query, 1));
            if (name == nullptr || relationText == nullptr) {
                success = false;
                break;
            }
            const int count = std::snprintf(message.data() + written,
                                            message.size() - written,
                                            "%s%s [%s]",
                                            written == 0 ? "" : "\n",
                                            name,
                                            relationText);
            if (count < 0 || static_cast<std::size_t>(count) >= message.size() - written) {
                success = false;
                break;
            }
            written += static_cast<std::size_t>(count);
        }
        if (success && written == 0) {
            success = format_message(message, written, "No friends or pending requests.");
        }
        ReleaseSRWLockExclusive(&g_lock);
        return success;
    }

    if (action == DiscordAction::accountRename) {
        const bool success = rename_account_locked(localAccountId, argument, nowSeconds, message, written);
        ReleaseSRWLockExclusive(&g_lock);
        return success;
    }

    std::array<char, kAccountNameCapacity> targetName{};
    sqlite3_int64 targetAccountId = 0;
    if (!valid_account_name(argument)
        || !account_for_name(g_database, argument, targetAccountId, targetName)
        || targetAccountId == localAccountId) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "Account not found.");
        return false;
    }
    if (!execute(g_database, "BEGIN IMMEDIATE;")) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    const char* outcome = nullptr;
    bool success = apply_relationship(
        g_database, localAccountId, targetAccountId, relationship_of(action), nowSeconds, outcome);
    success = success && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!success || outcome == nullptr) {
        (void)format_message(message, written, "That relationship action is not available.");
        return false;
    }
    return format_message(message, written, outcome, targetName.data());
}

bool execute_relationship(std::uint64_t platformId,
                          RelationshipAction action,
                          std::string_view targetName,
                          std::uint64_t nowSeconds,
                          std::span<char> message,
                          std::size_t& written) noexcept {
    written = 0;
    if (platformId == 0 || nowSeconds == 0) {
        (void)format_message(message, written, "Sign in to Dawn first.");
        return false;
    }

    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 actorAccountId = 0;
    std::array<char, kAccountNameCapacity> actorName{};
    if (g_database == nullptr
        || !account_for_platform(g_database, platformId, actorAccountId, actorName)) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "Play Dawn once on this PC first.");
        return false;
    }

    std::array<char, kAccountNameCapacity> canonicalName{};
    sqlite3_int64 targetAccountId = 0;
    if (!valid_account_name(targetName)
        || !account_for_name(g_database, targetName, targetAccountId, canonicalName)
        || targetAccountId == actorAccountId) {
        ReleaseSRWLockExclusive(&g_lock);
        // same answer for no such account and that is you, so this can't be used as a name oracle
        (void)format_message(message, written, "Account not found.");
        return false;
    }

    if (action == RelationshipAction::add
        && !within_request_budget(g_database, actorAccountId, nowSeconds)) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "That is a lot of friend requests. Try again later.");
        return false;
    }

    if (!execute(g_database, "BEGIN IMMEDIATE;")) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "The server is busy. Try again.");
        return false;
    }
    const char* outcome = nullptr;
    bool success =
        apply_relationship(g_database, actorAccountId, targetAccountId, action, nowSeconds, outcome);
    success = success && execute(g_database, "COMMIT;");
    if (!success) {
        (void)execute(g_database, "ROLLBACK;");
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (!success || outcome == nullptr) {
        (void)format_message(message, written, "That relationship action is not available.");
        return false;
    }
    return format_message(message, written, outcome, canonicalName.data());
}

bool send_direct_message(std::uint64_t platformId,
                         std::string_view targetName,
                         std::string_view body,
                         std::uint64_t nowSeconds,
                         std::span<char> message,
                         std::size_t& written) noexcept {
    written = 0;
    std::array<char, kDirectMessageCapacity> clean{};
    const std::size_t length = sanitize_message(body, clean);
    if (length == 0) {
        (void)format_message(message, written, "Type something to send.");
        return false;
    }
    if (platformId == 0 || nowSeconds == 0) {
        (void)format_message(message, written, "Sign in to Dawn first.");
        return false;
    }

    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 actorAccountId = 0;
    std::array<char, kAccountNameCapacity> actorName{};
    std::array<char, kAccountNameCapacity> canonicalName{};
    sqlite3_int64 targetAccountId = 0;
    if (g_database == nullptr
        || !account_for_platform(g_database, platformId, actorAccountId, actorName)) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "Play Dawn once on this PC first.");
        return false;
    }
    if (!valid_account_name(targetName)
        || !account_for_name(g_database, targetName, targetAccountId, canonicalName)
        || targetAccountId == actorAccountId) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "Account not found.");
        return false;
    }

    sqlite3_int64 requester = 0;
    sqlite3_int64 recipient = 0;
    int state = 0;
    if (!relation(g_database, actorAccountId, targetAccountId, requester, recipient, state)
        || state != 2) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "You can only message friends.");
        return false;
    }
    if (!within_message_budget(g_database, actorAccountId, nowSeconds)) {
        ReleaseSRWLockExclusive(&g_lock);
        (void)format_message(message, written, "You are sending messages too quickly.");
        return false;
    }

    bool success = execute(g_database, "BEGIN IMMEDIATE;");
    if (success) {
        Statement statement(
            g_database,
            "INSERT INTO direct_messages(from_account_id,to_account_id,body,sent_at)"
            " VALUES(?1,?2,?3,?4);");
        sqlite3_stmt* insert = statement.get();
        success = insert != nullptr
                  && sqlite3_bind_int64(insert, 1, actorAccountId) == SQLITE_OK
                  && sqlite3_bind_int64(insert, 2, targetAccountId) == SQLITE_OK
                  && bind_text(insert, 3, std::string_view(clean.data(), length))
                  && sqlite3_bind_int64(insert, 4, static_cast<sqlite3_int64>(nowSeconds))
                         == SQLITE_OK
                  && sqlite3_step(insert) == SQLITE_DONE;
        success = success && execute(g_database, "COMMIT;");
        if (!success) {
            (void)execute(g_database, "ROLLBACK;");
        }
    }
    if (success) {
        Statement prune(g_database, "DELETE FROM direct_messages WHERE sent_at<?1;");
        sqlite3_stmt* remove = prune.get();
        if (remove != nullptr) {
            const auto cutoff = static_cast<sqlite3_int64>(
                nowSeconds > kMessageRetention ? nowSeconds - kMessageRetention : 0);
            if (sqlite3_bind_int64(remove, 1, cutoff) == SQLITE_OK) {
                (void)sqlite3_step(remove);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (!success) {
        (void)format_message(message, written, "The message could not be sent.");
        return false;
    }
    return format_message(message, written, "Sent to %s.", canonicalName.data());
}

bool poll_direct_messages(std::uint64_t platformId,
                          std::uint64_t cursor,
                          std::span<DirectMessage> output,
                          std::size_t& count,
                          std::uint64_t& nextCursor) noexcept {
    count = 0;
    nextCursor = cursor;
    if (platformId == 0 || output.empty()) {
        return false;
    }

    AcquireSRWLockShared(&g_lock);
    sqlite3_int64 actorAccountId = 0;
    std::array<char, kAccountNameCapacity> actorName{};
    if (g_database == nullptr
        || !account_for_platform(g_database, platformId, actorAccountId, actorName)) {
        ReleaseSRWLockShared(&g_lock);
        return false;
    }

    Statement statement(
        g_database,
        "SELECT m.id,m.sent_at,m.from_account_id=?1,a.display_name,p.external_id,m.body"
        " FROM direct_messages m"
        " JOIN accounts a ON a.id=CASE WHEN m.from_account_id=?1 THEN m.to_account_id"
        " ELSE m.from_account_id END"
        " LEFT JOIN platform_identities p ON p.account_id=a.id AND p.provider=?2"
        " JOIN friendships f ON f.state=2 AND"
        " ((f.requester_account_id=?1 AND f.recipient_account_id=a.id)"
        " OR (f.recipient_account_id=?1 AND f.requester_account_id=a.id))"
        " WHERE (m.from_account_id=?1 OR m.to_account_id=?1) AND m.id>?3"
        " ORDER BY m.id LIMIT ?4;");
    sqlite3_stmt* query = statement.get();
    const auto limit = static_cast<int>((std::min)(output.size(), kDirectMessagePage));
    bool success = query != nullptr && sqlite3_bind_int64(query, 1, actorAccountId) == SQLITE_OK
                   && bind_text(query, 2, kProvider)
                   && sqlite3_bind_int64(query, 3, static_cast<sqlite3_int64>(cursor)) == SQLITE_OK
                   && sqlite3_bind_int(query, 4, limit) == SQLITE_OK;

    while (success && count < static_cast<std::size_t>(limit)) {
        const int step = sqlite3_step(query);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            success = false;
            break;
        }
        DirectMessage& row = output[count];
        row = {};
        row.id = static_cast<std::uint64_t>(sqlite3_column_int64(query, 0));
        row.sentAt = static_cast<std::uint64_t>(sqlite3_column_int64(query, 1));
        row.outgoing = sqlite3_column_int(query, 2) != 0;
        if (!copy_column_text(query, 3, row.peerName)) {
            success = false;
            break;
        }
        if (sqlite3_column_type(query, 4) == SQLITE_BLOB
            && sqlite3_column_bytes(query, 4) == static_cast<int>(sizeof(std::uint64_t))) {
            std::memcpy(&row.peerPlatformId, sqlite3_column_blob(query, 4), sizeof(std::uint64_t));
        }
        if (!copy_column_text(query, 5, row.body)) {
            success = false;
            break;
        }
        nextCursor = row.id;
        ++count;
    }
    ReleaseSRWLockShared(&g_lock);

    if (!success) {
        count = 0;
        nextCursor = cursor;
    }
    return success;
}

[[nodiscard]] bool valid_avatar(std::string_view value) noexcept {
    if (value.size() >= kAvatarCapacity) {
        return false;
    }
    const std::size_t slash = value.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= value.size()) {
        return false;
    }
    const std::string_view id = value.substr(0, slash);
    const std::string_view hash = value.substr(slash + 1);
    if (id.size() > 20
        || !std::all_of(id.begin(), id.end(), [](char c) noexcept { return c >= '0' && c <= '9'; })) {
        return false;
    }
    return hash.size() <= 40 && std::all_of(hash.begin(), hash.end(), [](char c) noexcept {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '_';
           });
}

bool exchange_avatars(std::uint64_t platformId,
                      std::string_view avatar,
                      std::uint64_t nowSeconds,
                      std::span<FriendAvatar> output,
                      std::size_t& count) noexcept {
    count = 0;
    if (platformId == 0 || output.empty()) {
        return false;
    }

    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 accountId = 0;
    std::array<char, kAccountNameCapacity> name{};
    if (g_database == nullptr
        || !account_for_platform(g_database, platformId, accountId, name)) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    if (!avatar.empty() && valid_avatar(avatar)) {
        Statement statement(
            g_database,
            "INSERT INTO account_avatars(account_id,avatar,updated_at) VALUES(?1,?2,?3)"
            " ON CONFLICT(account_id) DO UPDATE SET avatar=excluded.avatar,"
            " updated_at=excluded.updated_at;");
        sqlite3_stmt* upsert = statement.get();
        if (upsert != nullptr && sqlite3_bind_int64(upsert, 1, accountId) == SQLITE_OK
            && bind_text(upsert, 2, avatar)
            && sqlite3_bind_int64(upsert, 3, static_cast<sqlite3_int64>(nowSeconds)) == SQLITE_OK) {
            (void)sqlite3_step(upsert);
        }
    }

    Statement statement(
        g_database,
        "SELECT p.external_id,v.avatar FROM friendships f"
        " JOIN accounts a ON a.id=CASE WHEN f.requester_account_id=?1"
        " THEN f.recipient_account_id ELSE f.requester_account_id END"
        " JOIN platform_identities p ON p.account_id=a.id AND p.provider=?2"
        " JOIN account_avatars v ON v.account_id=a.id"
        " WHERE f.state=2 AND (f.requester_account_id=?1 OR f.recipient_account_id=?1)"
        " LIMIT ?3;");
    sqlite3_stmt* query = statement.get();
    bool success = query != nullptr && sqlite3_bind_int64(query, 1, accountId) == SQLITE_OK
                   && bind_text(query, 2, kProvider)
                   && sqlite3_bind_int(query, 3, static_cast<int>(output.size())) == SQLITE_OK;
    while (success && count < output.size()) {
        const int step = sqlite3_step(query);
        if (step == SQLITE_DONE) {
            break;
        }
        if (step != SQLITE_ROW) {
            success = false;
            break;
        }
        FriendAvatar& row = output[count];
        row = {};
        if (!read_u64_blob(query, 0, row.platformId) || !copy_column_text(query, 1, row.avatar)) {
            success = false;
            break;
        }
        ++count;
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!success) {
        count = 0;
    }
    return success;
}

bool social_snapshot(std::uint64_t platformId,
                     dawn::state::social::Snapshot& output) noexcept {
    output = {};
    if (platformId == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    sqlite3_int64 accountId = 0;
    std::array<char, kAccountNameCapacity> name{};
    const bool success = g_database != nullptr
                         && account_for_platform(g_database, platformId, accountId, name)
                         && load_social(g_database, accountId, platformId, output);
    ReleaseSRWLockShared(&g_lock);
    if (!success) {
        return false;
    }
    const auto nowValue = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    if (nowValue > 0) {
        for (std::size_t index = 0; index < output.remoteCount; ++index) {
            auto& remote = output.remotes[index];
            realtime_store::Presence live{};
            if (!realtime_store::presence(remote.platformId,
                                          static_cast<std::uint64_t>(nowValue),
                                          live)) {
                continue;
            }
            remote.personaState = 1;
            remote.appId = kDestinyAppId;
            remote.lobbyId = live.lobbyId;
            remote.joinable = live.joinable;
            if (live.status[0] != '\0') {
                remote.location = live.status;
            } else {
                std::copy(kDefaultLocation.begin(), kDefaultLocation.end(),
                          remote.location.begin());
            }
            remote.steamDisplay = live.steamDisplay;
            remote.connect = live.connect;
            remote.playerGroup = live.playerGroup;
            remote.playerGroupSize = live.playerGroupSize;
        }
    }
    return dawn::state::social::valid(output);
}

bool live_friend(std::uint64_t viewerAccountSoid,
                 std::uint64_t targetAccountSoid,
                 dawn::state::social::RemoteAccount& output) noexcept {
    output = {};
    if (viewerAccountSoid == 0 || targetAccountSoid == 0
        || viewerAccountSoid == targetAccountSoid) {
        return false;
    }

    std::uint64_t viewerPlatformId = 0;
    AcquireSRWLockShared(&g_lock);
    Statement statement(
        g_database,
        "SELECT identity.external_id FROM accounts account"
        " JOIN platform_identities identity ON identity.account_id=account.id"
        " WHERE account.account_soid=?1 AND identity.provider=?2 LIMIT 1;");
    sqlite3_stmt* query = statement.get();
    const bool resolved = g_database != nullptr && query != nullptr
                          && bind_u64_blob(query, 1, viewerAccountSoid)
                          && bind_text(query, 2, kProvider)
                          && sqlite3_step(query) == SQLITE_ROW
                          && read_u64_blob(query, 0, viewerPlatformId)
                          && viewerPlatformId != 0;
    ReleaseSRWLockShared(&g_lock);
    if (!resolved) {
        return false;
    }

    dawn::state::social::Snapshot social{};
    if (!social_snapshot(viewerPlatformId, social)) {
        return false;
    }
    for (std::size_t index = 0; index < social.remoteCount; ++index) {
        const auto& remote = social.remotes[index];
        if (remote.accountSoid == targetAccountSoid
            && remote.relationship == dawn::state::social::Relationship::friendAccount) {
            output = remote;
            return true;
        }
    }
    return false;
}

bool accepted_friend(std::uint64_t leftPlatformId,
                     std::uint64_t rightPlatformId) noexcept {
    if (leftPlatformId == 0 || rightPlatformId == 0 || leftPlatformId == rightPlatformId) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    sqlite3_int64 leftAccountId = 0;
    sqlite3_int64 rightAccountId = 0;
    std::array<char, kAccountNameCapacity> leftName{};
    std::array<char, kAccountNameCapacity> rightName{};
    bool accepted = g_database != nullptr
                    && account_for_platform(
                        g_database, leftPlatformId, leftAccountId, leftName)
                    && account_for_platform(
                        g_database, rightPlatformId, rightAccountId, rightName);
    if (accepted) {
        Statement statement(
            g_database,
            "SELECT 1 FROM friendships WHERE state=2 AND"
            " ((requester_account_id=?1 AND recipient_account_id=?2) OR"
            " (requester_account_id=?2 AND recipient_account_id=?1)) LIMIT 1;");
        sqlite3_stmt* query = statement.get();
        accepted = query != nullptr
                   && sqlite3_bind_int64(query, 1, leftAccountId) == SQLITE_OK
                   && sqlite3_bind_int64(query, 2, rightAccountId) == SQLITE_OK
                   && sqlite3_step(query) == SQLITE_ROW;
    }
    ReleaseSRWLockShared(&g_lock);
    return accepted;
}

bool friend_profile(std::uint64_t viewerPlatformId,
                    std::uint64_t targetPlatformId,
                    dawn::state::AccountState& output,
                    std::uint64_t& revision) noexcept {
    output = {};
    revision = 0;
    if (viewerPlatformId == 0 || targetPlatformId == 0
        || viewerPlatformId == targetPlatformId) {
        return false;
    }
    // realtime operations may call accepted_friend while holding their lock; never invert the order
    const bool teammate = realtime_store::shares_fireteam(viewerPlatformId, targetPlatformId);
    AcquireSRWLockShared(&g_lock);
    sqlite3_int64 viewerAccountId = 0;
    sqlite3_int64 targetAccountId = 0;
    std::array<char, kAccountNameCapacity> viewerName{};
    std::array<char, kAccountNameCapacity> targetName{};
    bool success = g_database != nullptr
                   && account_for_platform(
                       g_database, viewerPlatformId, viewerAccountId, viewerName)
                   && account_for_platform(
                       g_database, targetPlatformId, targetAccountId, targetName);
    if (success) {
        sqlite3_int64 requester = 0;
        sqlite3_int64 recipient = 0;
        int relationship = 0;
        success = (teammate || (relation(g_database,
                           viewerAccountId,
                           targetAccountId,
                           requester,
                           recipient,
                           relationship)
                  && relationship == 2))
                  && load_account_by_id(g_database, targetAccountId, revision, output)
                         == LoadStatus::loaded;
        if (success) {
            prefer_live_account(targetAccountId, output, revision);
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return success;
}

bool profile_by_platform(std::uint64_t platformId,
                         dawn::state::AccountState& output,
                         std::uint64_t& revision,
                         std::array<char, kAccountNameCapacity>& displayName) noexcept {
    output = {};
    revision = 0;
    displayName = {};
    if (platformId == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    sqlite3_int64 accountId = 0;
    const bool success = g_database != nullptr
                          && account_for_platform(g_database, platformId, accountId, displayName)
                          && load_account_by_id(g_database, accountId, revision, output)
                                 == LoadStatus::loaded;
    if (success) {
        prefer_live_account(accountId, output, revision);
    }
    ReleaseSRWLockShared(&g_lock);
    return success;
}

bool profile_by_account(std::uint64_t accountSoid,
                        std::uint64_t& platformId,
                        dawn::state::AccountState& output,
                        std::uint64_t& revision,
                        std::array<char, kAccountNameCapacity>& displayName) noexcept {
    platformId = 0;
    output = {};
    revision = 0;
    displayName = {};
    if (accountSoid == 0) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    Statement statement(
        g_database,
        "SELECT account.id,account.display_name,identity.external_id"
        " FROM accounts account JOIN platform_identities identity"
        " ON identity.account_id=account.id"
        " WHERE account.account_soid=?1 AND identity.provider=?2 LIMIT 1;");
    sqlite3_stmt* query = statement.get();
    sqlite3_int64 accountId = 0;
    bool success = g_database != nullptr && query != nullptr
                   && bind_u64_blob(query, 1, accountSoid)
                   && bind_text(query, 2, kProvider)
                   && sqlite3_step(query) == SQLITE_ROW;
    if (success) {
        accountId = sqlite3_column_int64(query, 0);
        success = accountId > 0
                  && copy_column_text(query, 1, displayName)
                  && read_u64_blob(query, 2, platformId)
                  && sqlite3_step(query) == SQLITE_DONE
                  && load_account_by_id(g_database, accountId, revision, output)
                         == LoadStatus::loaded
                  && output.primarySoid == accountSoid;
        if (success) {
            prefer_live_account(accountId, output, revision);
        }
    }
    ReleaseSRWLockShared(&g_lock);
    if (!success) {
        platformId = 0;
        output = {};
        revision = 0;
        displayName = {};
    }
    return success;
}

bool presentation_by_account(std::uint64_t accountSoid, std::uint64_t& platformId,
                             std::array<char, kAccountNameCapacity>& displayName) noexcept {
    platformId = 0;
    displayName = {};
    if (!accountSoid) return false;
    AcquireSRWLockShared(&g_lock);
    bool success = false;
    if (g_database) {
        Statement statement(g_database,
            "SELECT a.display_name,p.external_id FROM accounts a"
            " JOIN platform_identities p ON p.account_id=a.id"
            " WHERE a.account_soid=?1 AND p.provider=?2 LIMIT 1;");
        auto* query = statement.get();
        success = query && bind_u64_blob(query, 1, accountSoid)
            && bind_text(query, 2, kProvider) && sqlite3_step(query) == SQLITE_ROW
            && copy_column_text(query, 0, displayName)
            && read_u64_blob(query, 1, platformId) && platformId
            && sqlite3_step(query) == SQLITE_DONE;
    }
    ReleaseSRWLockShared(&g_lock);
    if (!success) { platformId = 0; displayName = {}; }
    return success;
}

bool load_or_create_device(std::uint64_t deviceId,
                           std::uint64_t platformId,
                           std::span<const std::byte> publicKey,
                           DeviceBinding& output) noexcept {
    output = {};
    if (deviceId == 0 || platformId == 0
        || publicKey.size()
               != dawn::core::identity::device_identity::kPublicKeySize) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (g_database == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    sqlite3_int64 credentialAccountId = 0;
    DeviceStatus device = device_account_locked(deviceId, publicKey, credentialAccountId);
    bool created = false;
    if (device == DeviceStatus::missing) {
        sqlite3_int64 existingId = 0;
        std::uint64_t existingRevision = 0;
        dawn::state::AccountState existing{};
        const LoadStatus existingStatus =
            load_account(g_database,
                         platformId,
                         existingId,
                         existingRevision,
                         existing);
        if (existingStatus == LoadStatus::loaded) {
            if (!adopt_released_account_locked(deviceId, publicKey, existingId)) {
                report("device_existing_account", "fail");
                ReleaseSRWLockExclusive(&g_lock);
                return false;
            }
            report("device_adopted", "ok");
        } else if (existingStatus != LoadStatus::missing
                   || !create_device_account_locked(
                       deviceId, platformId, publicKey, credentialAccountId)) {
            report(existingStatus != LoadStatus::missing ? "device_existing_account"
                                                         : "device_create",
                   "fail");
            ReleaseSRWLockExclusive(&g_lock);
            return false;
        }
        device = device_account_locked(deviceId, publicKey, credentialAccountId);
        created = device == DeviceStatus::matched && existingStatus == LoadStatus::missing;
    }
    if (device != DeviceStatus::matched) {
        report("device_credential", "fail");
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    if (!bind_platform_identity_locked(platformId, credentialAccountId)) {
        report("device_bind", "fail");
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    sqlite3_int64 loadedAccountId = 0;
    std::uint64_t revision = 0;
    dawn::state::AccountState account{};
    const LoadStatus accountStatus = load_account(
        g_database, platformId, loadedAccountId, revision, account);
    if (accountStatus != LoadStatus::loaded || loadedAccountId != credentialAccountId
        || !account_codec::self_test(account)) {
        report(accountStatus != LoadStatus::loaded ? "device_account_load"
               : loadedAccountId != credentialAccountId ? "device_account_mismatch"
                                                        : "device_account_self_test",
               "fail");
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    RuntimeRecord* record = find_runtime_record(account.primarySoid, platformId);
    if (record == nullptr) {
        auto candidate = std::make_unique<RuntimeRecord>();
        candidate->account = account;
        candidate->accountId = loadedAccountId;
        candidate->platformId = platformId;
        candidate->revision = revision;
        const LoadStatus activityStatus = load_activity(g_database,
                                                        loadedAccountId,
                                                        account.primarySoid,
                                                        candidate->activityRevision,
                                                        candidate->activity);
        if (activityStatus == LoadStatus::failed) {
            report("device_activity", "fail");
            ReleaseSRWLockExclusive(&g_lock);
            return false;
        }
        candidate->activity.defaults =
            dawn::core::settings::get().initialActivityDefaults;
        record = candidate.get();
        g_runtimeRecords.push_back(std::move(candidate));
    }
    output.runtime = runtime_context(*record);
    output.accountSoid = record->account.primarySoid;
    output.platformId = record->platformId;
    output.created = created;
    ReleaseSRWLockExclusive(&g_lock);
    report(created ? "device_enroll" : "device_load", "ok", record->revision);
    return true;
}

bool resolve_runtime_callback([[maybe_unused]] void* context,
                              std::uint64_t accountSoid,
                              std::uint64_t platformId,
                              dawn::state::AccountRuntimeContext& output) noexcept {
    output = {};
    AcquireSRWLockShared(&g_lock);
    RuntimeRecord* const record = find_runtime_record(accountSoid, platformId);
    if (record != nullptr) {
        output = runtime_context(*record);
    }
    ReleaseSRWLockShared(&g_lock);
    return record != nullptr;
}

bool resolve_friend_account_callback([[maybe_unused]] void* context,
                                     std::uint64_t viewerPlatformId,
                                     std::uint64_t targetPlatformId,
                                     std::uint64_t& targetAccountSoid) noexcept {
    targetAccountSoid = 0;
    if (viewerPlatformId == 0 || targetPlatformId == 0
        || viewerPlatformId == targetPlatformId) {
        return false;
    }
    const bool teammate = realtime_store::shares_fireteam(viewerPlatformId, targetPlatformId);
    AcquireSRWLockShared(&g_lock);
    Statement statement(
        g_database,
        "SELECT target.account_soid FROM platform_identities viewer_identity"
        " JOIN platform_identities target_identity"
        " ON target_identity.provider=viewer_identity.provider"
        " JOIN accounts target ON target.id=target_identity.account_id"
        " LEFT JOIN friendships friendship ON friendship.state=2 AND"
        " ((friendship.requester_account_id=viewer_identity.account_id AND"
        " friendship.recipient_account_id=target_identity.account_id) OR"
        " (friendship.requester_account_id=target_identity.account_id AND"
        " friendship.recipient_account_id=viewer_identity.account_id))"
        " WHERE viewer_identity.provider=?1 AND viewer_identity.external_id=?2"
        " AND target_identity.external_id=?3 AND (?4=1 OR friendship.state=2) LIMIT 1;");
    sqlite3_stmt* query = statement.get();
    const bool success = g_database != nullptr && query != nullptr
                         && bind_text(query, 1, kProvider)
                         && bind_u64_blob(query, 2, viewerPlatformId)
                         && bind_u64_blob(query, 3, targetPlatformId)
                         && sqlite3_bind_int(query, 4, teammate ? 1 : 0) == SQLITE_OK
                         && sqlite3_step(query) == SQLITE_ROW
                         && read_u64_blob(query, 0, targetAccountSoid)
                         && targetAccountSoid != 0;
    ReleaseSRWLockShared(&g_lock);
    if (!success) {
        targetAccountSoid = 0;
    }
    return success;
}

bool resolve_friend_profile_callback([[maybe_unused]] void* context,
                                     std::uint64_t viewerAccountSoid,
                                     std::uint64_t targetAccountSoid,
                                     dawn::state::AccountState& output) noexcept {
    output = {};
    if (viewerAccountSoid == 0 || targetAccountSoid == 0
        || viewerAccountSoid == targetAccountSoid) {
        return false;
    }
    const bool teammate = realtime_store::shares_fireteam_accounts(viewerAccountSoid, targetAccountSoid);
    AcquireSRWLockShared(&g_lock);
    Statement statement(
        g_database,
        "SELECT target.id FROM accounts viewer JOIN accounts target"
        " LEFT JOIN friendships friendship ON friendship.state=2 AND"
        " ((friendship.requester_account_id=viewer.id AND"
        " friendship.recipient_account_id=target.id) OR"
        " (friendship.requester_account_id=target.id AND"
        " friendship.recipient_account_id=viewer.id))"
        " WHERE viewer.account_soid=?1 AND target.account_soid=?2 AND (?3=1 OR friendship.state=2) LIMIT 1;");
    sqlite3_stmt* query = statement.get();
    sqlite3_int64 targetAccountId = 0;
    bool success = g_database != nullptr && query != nullptr
                   && bind_u64_blob(query, 1, viewerAccountSoid)
                   && bind_u64_blob(query, 2, targetAccountSoid)
                   && sqlite3_bind_int(query, 3, teammate ? 1 : 0) == SQLITE_OK
                         && sqlite3_step(query) == SQLITE_ROW;
    if (success) {
        targetAccountId = sqlite3_column_int64(query, 0);
        std::uint64_t revision = 0;
        success = targetAccountId > 0
                  && load_account_by_id(g_database, targetAccountId, revision, output)
                         == LoadStatus::loaded
                  && output.primarySoid == targetAccountSoid;
        if (success) {
            prefer_live_account(targetAccountId, output, revision);
        }
    }
    ReleaseSRWLockShared(&g_lock);
    if (!success) {
        output = {};
    }
    return success;
}

ExpansionOwnership expansion_ownership(std::uint64_t platformId) noexcept {
    ExpansionOwnership result{};
    if (platformId == 0) {
        return result;
    }
    AcquireSRWLockShared(&g_lock);
    if (g_database != nullptr) {
        Statement statement(
            g_database,
            "SELECT e.forsaken,e.shadowkeep FROM steam_expansion_pins e"
            " JOIN platform_identities p ON p.account_id=e.account_id"
            " WHERE p.provider=?1 AND p.external_id=?2;");
        sqlite3_stmt* query = statement.get();
        if (bind_text(query, 1, kProvider) && bind_u64_blob(query, 2, platformId)
            && sqlite3_step(query) == SQLITE_ROW) {
            result.known = true;
            result.forsaken = sqlite3_column_int(query, 0) != 0;
            result.shadowkeep = sqlite3_column_int(query, 1) != 0;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return result;
}

ExpansionRecord record_expansion_check(std::string_view discordUserId,
                                       std::uint64_t steamId,
                                       std::string_view steamAccountName,
                                       bool forsaken,
                                       bool shadowkeep,
                                       std::uint64_t nowSeconds,
                                       std::uint64_t& pinnedSteamId) noexcept {
    pinnedSteamId = 0;
    if (!valid_discord_user_id(discordUserId) || steamId == 0 || steamAccountName.empty()
        || steamAccountName.size() >= kAccountNameCapacity || nowSeconds == 0) {
        return ExpansionRecord::failed;
    }
    AcquireSRWLockExclusive(&g_lock);
    sqlite3_int64 accountId = 0;
    std::array<char, kAccountNameCapacity> accountName{};
    if (g_database == nullptr) {
        ReleaseSRWLockExclusive(&g_lock);
        return ExpansionRecord::failed;
    }
    if (!account_for_discord(g_database, discordUserId, accountId, accountName)) {
        ReleaseSRWLockExclusive(&g_lock);
        report("expansion_check", "not_linked");
        return ExpansionRecord::notLinked;
    }
    if (!execute(g_database, "BEGIN IMMEDIATE;")) {
        ReleaseSRWLockExclusive(&g_lock);
        return ExpansionRecord::failed;
    }

    ExpansionRecord outcome = ExpansionRecord::ok;
    {
        Statement pinned(g_database, "SELECT steam_id FROM steam_expansion_pins WHERE account_id=?1;");
        sqlite3_stmt* query = pinned.get();
        const int step = query != nullptr && sqlite3_bind_int64(query, 1, accountId) == SQLITE_OK
                             ? sqlite3_step(query)
                             : SQLITE_ERROR;
        if (step == SQLITE_ROW) {
            if (sqlite3_column_bytes(query, 0) == sizeof pinnedSteamId
                && sqlite3_column_blob(query, 0) != nullptr) {
                std::memcpy(&pinnedSteamId, sqlite3_column_blob(query, 0), sizeof pinnedSteamId);
            }
            if (pinnedSteamId != steamId) {
                outcome = ExpansionRecord::steamMismatch;
            }
        } else if (step != SQLITE_DONE) {
            outcome = ExpansionRecord::failed;
        }
    }
    if (outcome == ExpansionRecord::ok) {
        Statement owner(g_database, "SELECT account_id FROM steam_expansion_pins WHERE steam_id=?1;");
        sqlite3_stmt* query = owner.get();
        const int step = bind_u64_blob(query, 1, steamId) ? sqlite3_step(query) : SQLITE_ERROR;
        if (step == SQLITE_ROW && sqlite3_column_int64(query, 0) != accountId) {
            outcome = ExpansionRecord::steamInUse;
        } else if (step != SQLITE_ROW && step != SQLITE_DONE) {
            outcome = ExpansionRecord::failed;
        }
    }
    if (outcome == ExpansionRecord::ok) {
        Statement upsert(
            g_database,
            "INSERT INTO steam_expansion_pins(account_id,steam_id,steam_account_name,forsaken,"
            "shadowkeep,pinned_at,checked_at) VALUES(?1,?2,?3,?4,?5,?6,?6)"
            " ON CONFLICT(account_id) DO UPDATE SET steam_account_name=excluded.steam_account_name,"
            "forsaken=excluded.forsaken,shadowkeep=excluded.shadowkeep,checked_at=excluded.checked_at;");
        sqlite3_stmt* query = upsert.get();
        const bool written = query != nullptr
                             && sqlite3_bind_int64(query, 1, accountId) == SQLITE_OK
                             && bind_u64_blob(query, 2, steamId)
                             && bind_text(query, 3, steamAccountName)
                             && sqlite3_bind_int(query, 4, forsaken ? 1 : 0) == SQLITE_OK
                             && sqlite3_bind_int(query, 5, shadowkeep ? 1 : 0) == SQLITE_OK
                             && sqlite3_bind_int64(query, 6, static_cast<sqlite3_int64>(nowSeconds))
                                    == SQLITE_OK
                             && sqlite3_step(query) == SQLITE_DONE;
        if (written) {
            pinnedSteamId = steamId;
        } else {
            outcome = ExpansionRecord::failed;
        }
    }
    if (outcome == ExpansionRecord::ok) {
        if (!execute(g_database, "COMMIT;")) {
            (void)execute(g_database, "ROLLBACK;");
            outcome = ExpansionRecord::failed;
        }
    } else {
        (void)execute(g_database, "ROLLBACK;");
    }
    ReleaseSRWLockExclusive(&g_lock);
    report("expansion_check", outcome == ExpansionRecord::ok ? "ok" : "refused");
    return outcome;
}

bool set_discord_roles(std::string_view discordUserId,
                       std::uint32_t roleMask,
                       std::uint64_t nowSeconds) noexcept {
    if (!valid_discord_user_id(discordUserId) || nowSeconds == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr && roleMask == 0) {
        Statement remove(g_database, "DELETE FROM discord_member_roles WHERE discord_user_id=?1;");
        success = bind_text(remove.get(), 1, discordUserId) && sqlite3_step(remove.get()) == SQLITE_DONE;
    } else if (g_database != nullptr) {
        Statement upsert(
            g_database,
            "INSERT INTO discord_member_roles(discord_user_id,role_mask,updated_at) VALUES(?1,?2,?3)"
            " ON CONFLICT(discord_user_id) DO UPDATE SET role_mask=excluded.role_mask,"
            "updated_at=excluded.updated_at;");
        sqlite3_stmt* query = upsert.get();
        success = bind_text(query, 1, discordUserId)
                  && sqlite3_bind_int64(query, 2, static_cast<sqlite3_int64>(roleMask)) == SQLITE_OK
                  && sqlite3_bind_int64(query, 3, static_cast<sqlite3_int64>(nowSeconds)) == SQLITE_OK
                  && sqlite3_step(query) == SQLITE_DONE;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

bool prune_discord_roles(std::uint64_t syncStartedAt) noexcept {
    if (syncStartedAt == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr) {
        Statement prune(g_database, "DELETE FROM discord_member_roles WHERE updated_at<?1;");
        success = sqlite3_bind_int64(prune.get(), 1, static_cast<sqlite3_int64>(syncStartedAt)) == SQLITE_OK
                  && sqlite3_step(prune.get()) == SQLITE_DONE;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

std::uint32_t discord_role_mask(std::string_view discordUserId) noexcept {
    if (!valid_discord_user_id(discordUserId)) {
        return 0;
    }
    AcquireSRWLockShared(&g_lock);
    std::uint32_t mask = 0;
    if (g_database != nullptr) {
        Statement statement(g_database, "SELECT role_mask FROM discord_member_roles WHERE discord_user_id=?1;");
        sqlite3_stmt* query = statement.get();
        if (bind_text(query, 1, discordUserId) && sqlite3_step(query) == SQLITE_ROW) {
            mask = static_cast<std::uint32_t>(sqlite3_column_int64(query, 0));
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return mask;
}

std::uint32_t discord_access_grants(std::string_view discordUserId) noexcept {
    if (!valid_discord_user_id(discordUserId)) {
        return 0;
    }
    AcquireSRWLockShared(&g_lock);
    std::uint32_t grants = 0;
    if (g_database != nullptr) {
        Statement statement(g_database, "SELECT grants FROM discord_access_grants WHERE discord_user_id=?1;");
        sqlite3_stmt* query = statement.get();
        if (bind_text(query, 1, discordUserId) && sqlite3_step(query) == SQLITE_ROW) {
            grants = static_cast<std::uint32_t>(sqlite3_column_int64(query, 0));
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return grants;
}

bool discord_linked_account(std::string_view discordUserId,
                            std::array<char, kAccountNameCapacity>& accountName) noexcept {
    accountName.fill('\0');
    if (!valid_discord_user_id(discordUserId)) {
        return false;
    }
    AcquireSRWLockShared(&g_lock);
    sqlite3_int64 accountId = 0;
    const bool linked = g_database != nullptr
                        && account_for_discord(g_database, discordUserId, accountId, accountName);
    ReleaseSRWLockShared(&g_lock);
    return linked;
}

bool create_access_code(std::span<const std::byte> codeHash,
                        std::uint32_t grants,
                        std::string_view createdBy,
                        std::uint64_t nowSeconds) noexcept {
    if (codeHash.size() != kDigestSize || grants == 0 || !valid_discord_user_id(createdBy)
        || nowSeconds == 0) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr) {
        Statement insert(
            g_database,
            "INSERT INTO access_codes(code_hash,grants,created_by,created_at) VALUES(?1,?2,?3,?4);");
        sqlite3_stmt* query = insert.get();
        success = bind_bytes(query, 1, codeHash)
                  && sqlite3_bind_int64(query, 2, static_cast<sqlite3_int64>(grants)) == SQLITE_OK
                  && bind_text(query, 3, createdBy)
                  && sqlite3_bind_int64(query, 4, static_cast<sqlite3_int64>(nowSeconds)) == SQLITE_OK
                  && sqlite3_step(query) == SQLITE_DONE;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

AccessCodeRedeem redeem_access_code(std::span<const std::byte> codeHash,
                                    std::string_view discordUserId,
                                    std::uint64_t nowSeconds,
                                    std::uint32_t& grants) noexcept {
    grants = 0;
    if (codeHash.size() != kDigestSize || !valid_discord_user_id(discordUserId) || nowSeconds == 0) {
        return AccessCodeRedeem::failed;
    }
    AcquireSRWLockExclusive(&g_lock);
    if (g_database == nullptr || !execute(g_database, "BEGIN IMMEDIATE;")) {
        ReleaseSRWLockExclusive(&g_lock);
        return AccessCodeRedeem::failed;
    }
    AccessCodeRedeem outcome = AccessCodeRedeem::ok;
    std::uint32_t codeGrants = 0;
    bool alreadyMine = false;
    {
        Statement select(g_database, "SELECT grants,redeemed_by FROM access_codes WHERE code_hash=?1;");
        sqlite3_stmt* query = select.get();
        const int step = bind_bytes(query, 1, codeHash) ? sqlite3_step(query) : SQLITE_ERROR;
        if (step == SQLITE_DONE) {
            outcome = AccessCodeRedeem::notFound;
        } else if (step != SQLITE_ROW) {
            outcome = AccessCodeRedeem::failed;
        } else {
            codeGrants = static_cast<std::uint32_t>(sqlite3_column_int64(query, 0));
            if (sqlite3_column_type(query, 1) != SQLITE_NULL) {
                const auto* redeemer = reinterpret_cast<const char*>(sqlite3_column_text(query, 1));
                const int size = sqlite3_column_bytes(query, 1);
                alreadyMine = redeemer != nullptr && size >= 0
                              && std::string_view(redeemer, static_cast<std::size_t>(size)) == discordUserId;
                if (!alreadyMine) {
                    outcome = AccessCodeRedeem::alreadyUsed;
                }
            }
        }
    }
    if (outcome == AccessCodeRedeem::ok && !alreadyMine) {
        Statement mark(g_database, "UPDATE access_codes SET redeemed_by=?2,redeemed_at=?3 WHERE code_hash=?1;");
        sqlite3_stmt* query = mark.get();
        const bool marked = bind_bytes(query, 1, codeHash) && bind_text(query, 2, discordUserId)
                            && sqlite3_bind_int64(query, 3, static_cast<sqlite3_int64>(nowSeconds)) == SQLITE_OK
                            && sqlite3_step(query) == SQLITE_DONE;
        if (!marked) {
            outcome = AccessCodeRedeem::failed;
        }
    }
    if (outcome == AccessCodeRedeem::ok) {
        Statement grant(
            g_database,
            "INSERT INTO discord_access_grants(discord_user_id,grants,updated_at) VALUES(?1,?2,?3)"
            " ON CONFLICT(discord_user_id) DO UPDATE SET grants=(grants|excluded.grants),"
            "updated_at=excluded.updated_at;");
        sqlite3_stmt* query = grant.get();
        const bool granted = bind_text(query, 1, discordUserId)
                             && sqlite3_bind_int64(query, 2, static_cast<sqlite3_int64>(codeGrants)) == SQLITE_OK
                             && sqlite3_bind_int64(query, 3, static_cast<sqlite3_int64>(nowSeconds)) == SQLITE_OK
                             && sqlite3_step(query) == SQLITE_DONE;
        if (!granted) {
            outcome = AccessCodeRedeem::failed;
        }
    }
    if (outcome == AccessCodeRedeem::ok) {
        Statement total(g_database, "SELECT grants FROM discord_access_grants WHERE discord_user_id=?1;");
        sqlite3_stmt* query = total.get();
        if (bind_text(query, 1, discordUserId) && sqlite3_step(query) == SQLITE_ROW) {
            grants = static_cast<std::uint32_t>(sqlite3_column_int64(query, 0));
        }
    }
    if (outcome == AccessCodeRedeem::ok) {
        if (!execute(g_database, "COMMIT;")) {
            (void)execute(g_database, "ROLLBACK;");
            outcome = AccessCodeRedeem::failed;
            grants = 0;
        }
    } else {
        (void)execute(g_database, "ROLLBACK;");
    }
    ReleaseSRWLockExclusive(&g_lock);
    report("access_code_redeem", outcome == AccessCodeRedeem::ok ? "ok" : "refused");
    return outcome;
}

bool clear_discord_access_grants(std::string_view discordUserId) noexcept {
    if (!valid_discord_user_id(discordUserId)) {
        return false;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool success = false;
    if (g_database != nullptr) {
        Statement remove(g_database, "DELETE FROM discord_access_grants WHERE discord_user_id=?1;");
        success = bind_text(remove.get(), 1, discordUserId) && sqlite3_step(remove.get()) == SQLITE_DONE;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return success;
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_database != nullptr) {
        (void)sqlite3_wal_checkpoint_v2(
            g_database, nullptr, SQLITE_CHECKPOINT_FULL, nullptr, nullptr);
        (void)sqlite3_close_v2(g_database);
    }
    g_database = nullptr;
    g_accountId = 0;
    g_revision = 0;
    g_activityRevision = 0;
    g_runtimeRecords.clear();
    SecureZeroMemory(&g_starterSeed, sizeof g_starterSeed);
    ReleaseSRWLockExclusive(&g_lock);
    realtime_store::shutdown();
}

} // namespace restoration::account_store
