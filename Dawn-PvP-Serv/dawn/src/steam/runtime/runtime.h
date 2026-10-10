#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace dawn::steam {

// the owner build keeps the default; a transfer build can set a distinct identity by
// define
#ifndef RESTORATION_LOCAL_STEAM_ID
#define RESTORATION_LOCAL_STEAM_ID 0x0110000100000001ULL
#endif
inline constexpr std::uint64_t kBuildLocalSteamId = RESTORATION_LOCAL_STEAM_ID;

#if defined(RESTORATION_UNIVERSAL_CLIENT) && RESTORATION_UNIVERSAL_CLIENT
inline constexpr bool kUniversalClientBuild = true;
#else
inline constexpr bool kUniversalClientBuild = false;
#endif

[[nodiscard]] bool initialize_local_identity(void* module) noexcept;

[[nodiscard]] std::uint64_t local_steam_id() noexcept;

void shutdown_local_identity() noexcept;

using UserHandle = std::int32_t;

using PipeHandle = std::int32_t;

using ApiCall = std::uint64_t;

[[nodiscard]] bool initialize(void* module) noexcept;

[[nodiscard]] bool shutdown() noexcept;

[[nodiscard]] bool is_running() noexcept;

void run_callbacks() noexcept;

void set_app_id(DWORD appId) noexcept;

[[nodiscard]] DWORD app_id() noexcept;

[[nodiscard]] ApiCall next_api_call() noexcept;

[[nodiscard]] bool
queue_callback(int callbackId, ApiCall call, const void* payload, std::size_t payloadSize) noexcept;

[[nodiscard]] std::size_t registered_callback_count(int callbackId,
                                                    std::size_t payloadSize) noexcept;

// payload bytes after the private callback's 16-byte rendezvous envelope
inline constexpr std::size_t kSerializedRendezvousPacketCapacity = 0x200U;
// size returned by the archived DLL's rendezvous callback vtable slot two
inline constexpr std::size_t kSerializedRendezvousCallbackSize =
    sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t)
    + kSerializedRendezvousPacketCapacity;

[[nodiscard]] bool queue_serialized_rendezvous(std::uint64_t remotePlatformId,
                                               std::uint32_t sourceConnectionId,
                                               const void* payload,
                                               std::size_t payloadSize) noexcept;

[[nodiscard]] UserHandle user_handle() noexcept;

[[nodiscard]] PipeHandle pipe_handle() noexcept;

void register_callback(void* callback, int callbackId) noexcept;

void unregister_callback(void* callback) noexcept;

void register_call_result(void* callback, ApiCall call) noexcept;

void unregister_call_result(void* callback, ApiCall call) noexcept;

[[nodiscard]] void* context_init(void* data) noexcept;

[[nodiscard]] void* create_interface(const char* version, const void* callerAddress) noexcept;

[[nodiscard]] void* find_or_create_user_interface(UserHandle user, const char* version) noexcept;

} // namespace dawn::steam
