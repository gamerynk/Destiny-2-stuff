#include "steam_transport_bridge.h"
#include "native_certificate_publish.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "../../../core/logging/log.h"
#include "../callbacks/callback_registry.h"

namespace dawn::steam::runtime::transport {
namespace {

constexpr DWORD kDestinyAppId = 1085660;
constexpr std::uint64_t kSteamIndividualPrefix = 0x0110000100000000ULL;
constexpr std::uint64_t kSteamTypeMask = 0xFFFFFFFF00000000ULL;
constexpr std::size_t kPathCapacity = 4096;
constexpr std::size_t kDocumentCapacity = 64U * 1024U;
constexpr std::size_t kMaximumCallbacksPerPump = 64;
constexpr std::size_t kMaximumCallbackPayload = 4096;
constexpr std::size_t kPendingCallCapacity = 8;
constexpr int kApiCallCompletedCallback = 703;
constexpr int kNetworkingCallbackFirst = 1200;
constexpr int kNetworkingCallbackLast = 1399;
constexpr int kHttpCallbackFirst = 2100;
constexpr int kHttpCallbackLast = 2199;

using Path = std::array<wchar_t, kPathCapacity>;

struct CallbackMessage final {
    UserHandle user{};
    int callback{};
    unsigned char* parameter{};
    int parameterBytes{};
};

struct ApiCallCompleted final {
    ApiCall call{};
    int callback{};
    std::uint32_t parameterBytes{};
};

static_assert(sizeof(ApiCallCompleted) == 16);

using InitializeFn = bool (*)();
using ShutdownFn = void (*)();
using GetPipeFn = PipeHandle (*)();
using GetUserFn = UserHandle (*)();
using SteamUserFn = void* (*)();
using LoggedOnFn = bool (*)(void*);
using SteamIdFn = std::uint64_t (*)(void*);
using SteamClientFn = void* (*)();
using SteamHttpFn = void* (*)();
using GenericInterfaceFn = void* (*)(void*, UserHandle, PipeHandle, const char*);
using UtilsInterfaceFn = void* (*)(void*, PipeHandle, const char*);
using DispatchInitializeFn = void (*)();
using DispatchFrameFn = void (*)(PipeHandle);
using DispatchNextFn = bool (*)(PipeHandle, CallbackMessage*);
using DispatchFreeFn = void (*)(PipeHandle);
using DispatchResultFn = bool (*)(PipeHandle, ApiCall, void*, int, int, bool*);

using CertificateFn = ApiCall (*)(void*);
using CountryFn = const char* (*)(void*);
using ServerRealTimeFn = std::uint32_t (*)(void*);
using NetworkConfigFn = int (*)(void*, void*, DWORD, const char*);
using CacheRelayTicketFn = void (*)(void*, const void*, DWORD);
using RelayTicketCountFn = DWORD (*)(void*);
using RelayTicketFn = int (*)(void*, DWORD, void*, DWORD);
using PostConnectionStateFn = void (*)(void*, const void*, DWORD);

struct Bridge final {
    HMODULE module{};
    void* serialized{};
    void* http{};
    void* utils{};
    PipeHandle pipe{};
    UserHandle user{};
    ShutdownFn shutdown{};
    DispatchFrameFn dispatchFrame{};
    DispatchNextFn dispatchNext{};
    DispatchFreeFn dispatchFree{};
    DispatchResultFn dispatchResult{};
    CertificateFn certificate{};
    CountryFn country{};
    ServerRealTimeFn serverRealTime{};
    NetworkConfigFn networkConfig{};
    CacheRelayTicketFn cacheRelayTicket{};
    RelayTicketCountFn relayTicketCount{};
    RelayTicketFn relayTicket{};
    PostConnectionStateFn postConnectionState{};
    std::array<ApiCall, kPendingCallCapacity> pending{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
Bridge g_bridge{};
std::atomic<std::uint64_t> g_platformId{};
std::atomic_bool g_ready{};
std::atomic_bool g_countryReported{};
std::atomic_bool g_serverRealTimeReported{};
bool g_reported{};

template <typename Function>
[[nodiscard]] Function export_function(HMODULE module, const char* name) noexcept {
    return module == nullptr ? nullptr
                             : reinterpret_cast<Function>(GetProcAddress(module, name));
}

template <typename Function>
[[nodiscard]] Function virtual_method(void* object, std::size_t slot) noexcept {
    if (object == nullptr) {
        return nullptr;
    }
    auto** table = *static_cast<void***>(object);
    return table == nullptr ? nullptr : reinterpret_cast<Function>(table[slot]);
}

[[nodiscard]] bool valid_platform_id(std::uint64_t value) noexcept {
    return (value & kSteamTypeMask) == kSteamIndividualPrefix
           && static_cast<std::uint32_t>(value) != 0;
}

[[nodiscard]] bool path_copy(Path& output, std::wstring_view value) noexcept {
    if (value.empty() || value.size() >= output.size()) {
        return false;
    }
    std::fill(output.begin(), output.end(), L'\0');
    std::copy(value.begin(), value.end(), output.begin());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (output[index] == L'/') {
            output[index] = L'\\';
        }
    }
    return true;
}

[[nodiscard]] bool path_append(Path& path, std::wstring_view suffix) noexcept {
    const auto end = std::find(path.cbegin(), path.cend(), L'\0');
    const std::size_t length = static_cast<std::size_t>(end - path.cbegin());
    if (end == path.cend() || suffix.size() >= path.size() - length) {
        return false;
    }
    std::copy(suffix.begin(), suffix.end(), path.begin() + static_cast<std::ptrdiff_t>(length));
    path[length + suffix.size()] = L'\0';
    return true;
}

[[nodiscard]] bool regular_file(const Path& path) noexcept {
    const DWORD attributes = GetFileAttributesW(path.data());
    return attributes != INVALID_FILE_ATTRIBUTES
           && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

[[nodiscard]] bool read_document(const Path& path,
                                 std::array<char, kDocumentCapacity>& output,
                                 std::size_t& size) noexcept {
    size = 0;
    output.fill('\0');
    HANDLE file = CreateFileW(path.data(),
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER length{};
    DWORD read = 0;
    const bool success = GetFileSizeEx(file, &length) != FALSE && length.QuadPart > 0
                         && length.QuadPart < static_cast<LONGLONG>(output.size())
                         && ReadFile(file,
                                     output.data(),
                                     static_cast<DWORD>(length.QuadPart),
                                     &read,
                                     nullptr)
                                != FALSE
                         && read == static_cast<DWORD>(length.QuadPart);
    CloseHandle(file);
    size = success ? static_cast<std::size_t>(read) : 0;
    return success;
}

[[nodiscard]] bool quoted_value(std::string_view document,
                                std::string_view key,
                                std::size_t& cursor,
                                Path& output) noexcept {
    const std::string_view marker = key;
    const std::size_t found = document.find(marker, cursor);
    if (found == std::string_view::npos) {
        return false;
    }
    std::size_t quote = document.find('"', found + marker.size());
    if (quote == std::string_view::npos) {
        return false;
    }
    ++quote;
    output.fill(L'\0');
    std::size_t written = 0;
    bool escaped = false;
    for (std::size_t index = quote; index < document.size(); ++index) {
        const char current = document[index];
        if (!escaped && current == '"') {
            cursor = index + 1;
            return written != 0;
        }
        if (!escaped && current == '\\') {
            escaped = true;
            continue;
        }
        if (written + 1 >= output.size()) {
            return false;
        }
        output[written++] = static_cast<unsigned char>(current);
        escaped = false;
    }
    return false;
}

[[nodiscard]] bool steam_root(Path& output) noexcept {
    output.fill(L'\0');
    DWORD bytes = static_cast<DWORD>(output.size() * sizeof(wchar_t));
    const LSTATUS result = RegGetValueW(HKEY_CURRENT_USER,
                                        L"Software\\Valve\\Steam",
                                        L"SteamPath",
                                        RRF_RT_REG_SZ,
                                        nullptr,
                                        output.data(),
                                        &bytes);
    if (result != ERROR_SUCCESS || output.front() == L'\0') {
        return false;
    }
    for (wchar_t& value : output) {
        if (value == L'\0') {
            break;
        }
        if (value == L'/') {
            value = L'\\';
        }
    }
    return true;
}

[[nodiscard]] bool api_in_library(const Path& library, Path& output) noexcept {
    Path manifest = library;
    if (!path_append(manifest, L"\\steamapps\\appmanifest_1085660.acf")) {
        return false;
    }
    std::array<char, kDocumentCapacity> document{};
    std::size_t documentSize = 0;
    if (!read_document(manifest, document, documentSize)) {
        return false;
    }
    Path installDirectory{};
    std::size_t cursor = 0;
    if (!quoted_value(std::string_view(document.data(), documentSize),
                      "\"installdir\"",
                      cursor,
                      installDirectory)) {
        return false;
    }
    output = library;
    return path_append(output, L"\\steamapps\\common\\")
           && path_append(output, installDirectory.data())
           && path_append(output, L"\\bin\\x64\\steam_api64.dll")
           && regular_file(output);
}

[[nodiscard]] bool locate_api(Path& output) noexcept {
    // a depot download has no appmanifest for Destiny 2, so let the launcher point at a real Steam API DLL
    output.fill(L'\0');
    const DWORD overrideLength = GetEnvironmentVariableW(
        L"RESTORATION_STEAM_API_PATH", output.data(), static_cast<DWORD>(output.size()));
    if (overrideLength != 0 && overrideLength < output.size() && regular_file(output)) {
        return true;
    }
    Path root{};
    if (!steam_root(root)) {
        return false;
    }
    if (api_in_library(root, output)) {
        return true;
    }
    Path libraries = root;
    if (!path_append(libraries, L"\\steamapps\\libraryfolders.vdf")) {
        return false;
    }
    std::array<char, kDocumentCapacity> document{};
    std::size_t documentSize = 0;
    if (!read_document(libraries, document, documentSize)) {
        return false;
    }
    const std::string_view text(document.data(), documentSize);
    std::size_t cursor = 0;
    Path library{};
    while (quoted_value(text, "\"path\"", cursor, library)) {
        if (api_in_library(library, output)) {
            return true;
        }
    }
    Path steamVrApi = root;
    if (path_append(steamVrApi,
                    L"\\steamapps\\common\\SteamVR\\bin\\win64\\steam_api64.dll")
        && regular_file(steamVrApi)) {
        output = steamVrApi;
        return true;
    }
    return false;
}

[[nodiscard]] bool add_pending(ApiCall call) noexcept {
    for (ApiCall& entry : g_bridge.pending) {
        if (entry == 0) {
            entry = call;
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool remove_pending(ApiCall call) noexcept {
    for (ApiCall& entry : g_bridge.pending) {
        if (entry == call) {
            entry = 0;
            return true;
        }
    }
    return false;
}

void clear_bridge() noexcept {
    if (g_bridge.shutdown != nullptr) {
        g_bridge.shutdown();
    }
    if (g_bridge.module != nullptr) {
        FreeLibrary(g_bridge.module);
    }
    g_bridge = {};
    g_platformId.store(0, std::memory_order_release);
    g_ready.store(false, std::memory_order_release);
    g_countryReported.store(false, std::memory_order_release);
    g_serverRealTimeReported.store(false, std::memory_order_release);
}

} // namespace

bool initialize(DWORD appId) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (g_ready.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_lock);
        return true;
    }
    if (appId == 0) {
        appId = kDestinyAppId;
    }
    if (appId != kDestinyAppId) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    wchar_t appIdText[16]{};
    if (std::swprintf(appIdText, std::size(appIdText), L"%lu", appId) <= 0
        || SetEnvironmentVariableW(L"SteamAppId", appIdText) == FALSE
        || SetEnvironmentVariableW(L"SteamGameId", appIdText) == FALSE) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    Path apiPath{};
    if (!locate_api(apiPath)) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    HMODULE module = LoadLibraryExW(apiPath.data(),
                                    nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                                        | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto apiInitialize = export_function<InitializeFn>(module, "SteamAPI_InitSafe");
    const auto apiShutdown = export_function<ShutdownFn>(module, "SteamAPI_Shutdown");
    const auto getPipe = export_function<GetPipeFn>(module, "SteamAPI_GetHSteamPipe");
    const auto getUser = export_function<GetUserFn>(module, "SteamAPI_GetHSteamUser");
    const auto getSteamUser = export_function<SteamUserFn>(module, "SteamAPI_SteamUser_v023");
    const auto loggedOn = export_function<LoggedOnFn>(module, "SteamAPI_ISteamUser_BLoggedOn");
    const auto getSteamId = export_function<SteamIdFn>(module, "SteamAPI_ISteamUser_GetSteamID");
    const auto getSteamClient = export_function<SteamClientFn>(module, "SteamClient");
    const auto getSteamHttp = export_function<SteamHttpFn>(module, "SteamAPI_SteamHTTP_v003");
    const auto getSteamUtils = export_function<UtilsInterfaceFn>(
        module, "SteamAPI_ISteamClient_GetISteamUtils");
    const auto getGeneric = export_function<GenericInterfaceFn>(
        module, "SteamAPI_ISteamClient_GetISteamGenericInterface");
    const auto dispatchInitialize = export_function<DispatchInitializeFn>(
        module, "SteamAPI_ManualDispatch_Init");
    Bridge candidate{};
    candidate.module = module;
    candidate.shutdown = apiShutdown;
    candidate.dispatchFrame = export_function<DispatchFrameFn>(
        module, "SteamAPI_ManualDispatch_RunFrame");
    candidate.dispatchNext = export_function<DispatchNextFn>(
        module, "SteamAPI_ManualDispatch_GetNextCallback");
    candidate.dispatchFree = export_function<DispatchFreeFn>(
        module, "SteamAPI_ManualDispatch_FreeLastCallback");
    candidate.dispatchResult = export_function<DispatchResultFn>(
        module, "SteamAPI_ManualDispatch_GetAPICallResult");
    const bool exportsReady = module != nullptr && apiInitialize != nullptr
                              && apiShutdown != nullptr && getPipe != nullptr
                              && getUser != nullptr && getSteamUser != nullptr
                              && loggedOn != nullptr && getSteamId != nullptr
                              && getSteamClient != nullptr && getSteamHttp != nullptr
                              && getSteamUtils != nullptr
                              && getGeneric != nullptr
                              && dispatchInitialize != nullptr
                              && candidate.dispatchFrame != nullptr
                              && candidate.dispatchNext != nullptr
                              && candidate.dispatchFree != nullptr
                              && candidate.dispatchResult != nullptr;
    const bool apiReady = exportsReady && apiInitialize();
    candidate.pipe = apiReady ? getPipe() : 0;
    candidate.user = apiReady ? getUser() : 0;
    void* const steamUser = apiReady ? getSteamUser() : nullptr;
    void* const steamClient = apiReady ? getSteamClient() : nullptr;
    candidate.http = apiReady ? getSteamHttp() : nullptr;
    candidate.utils = apiReady && steamClient != nullptr
                          ? getSteamUtils(steamClient, candidate.pipe, "SteamUtils009")
                          : nullptr;
    const std::uint64_t platformId = steamUser != nullptr && loggedOn(steamUser)
                                         ? getSteamId(steamUser)
                                         : 0;
    candidate.serialized = steamClient != nullptr
                               ? getGeneric(steamClient,
                                            candidate.user,
                                            candidate.pipe,
                                            "SteamNetworkingSocketsSerialized003")
                               : nullptr;
    candidate.certificate = virtual_method<CertificateFn>(candidate.serialized, 2);
    candidate.serverRealTime = virtual_method<ServerRealTimeFn>(candidate.utils, 3);
    candidate.country = virtual_method<CountryFn>(candidate.utils, 4);
    candidate.networkConfig = virtual_method<NetworkConfigFn>(candidate.serialized, 3);
    candidate.cacheRelayTicket = virtual_method<CacheRelayTicketFn>(candidate.serialized, 4);
    candidate.relayTicketCount = virtual_method<RelayTicketCountFn>(candidate.serialized, 5);
    candidate.relayTicket = virtual_method<RelayTicketFn>(candidate.serialized, 6);
    candidate.postConnectionState = virtual_method<PostConnectionStateFn>(candidate.serialized, 7);
    const bool ready = apiReady && candidate.pipe != 0 && candidate.user != 0
                       && valid_platform_id(platformId) && candidate.serialized != nullptr
                       && candidate.http != nullptr && candidate.utils != nullptr
                       && candidate.certificate != nullptr && candidate.networkConfig != nullptr
                       && candidate.country != nullptr && candidate.serverRealTime != nullptr
                       && candidate.cacheRelayTicket != nullptr
                       && candidate.relayTicketCount != nullptr
                       && candidate.relayTicket != nullptr
                       && candidate.postConnectionState != nullptr;
    if (!ready) {
        if (apiReady && candidate.shutdown != nullptr) {
            candidate.shutdown();
        }
        if (candidate.module != nullptr) {
            FreeLibrary(candidate.module);
        }
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    dispatchInitialize();
    g_bridge = candidate;
    g_platformId.store(platformId, std::memory_order_release);
    g_ready.store(true, std::memory_order_release);
    g_countryReported.store(false, std::memory_order_release);
    g_serverRealTimeReported.store(false, std::memory_order_release);
    g_reported = false;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

std::uint64_t platform_id() noexcept {
    return g_platformId.load(std::memory_order_acquire);
}

void* http_interface() noexcept {
    AcquireSRWLockShared(&g_lock);
    void* const result = g_ready.load(std::memory_order_acquire) ? g_bridge.http : nullptr;
    ReleaseSRWLockShared(&g_lock);
    return result;
}

const char* country() noexcept {
    constexpr char kUnknownCountry[] = "";
    AcquireSRWLockShared(&g_lock);
    const char* const candidate =
        g_ready.load(std::memory_order_acquire) && g_bridge.country != nullptr
            ? g_bridge.country(g_bridge.utils)
            : nullptr;
    ReleaseSRWLockShared(&g_lock);
    const char* const result = candidate != nullptr ? candidate : kUnknownCountry;
    if (result[0] != '\0' && !g_countryReported.exchange(true, std::memory_order_acq_rel)) {
        std::array<char, 128> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=steam_transport stage=country result=ok value=%.8s",
                                          result);
        if (written > 0) {
            core::log::write(
                core::log::Channel::client,
                core::log::Level::info,
                {line.data(), (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
    }
    return result;
}

std::uint32_t server_real_time() noexcept {
    AcquireSRWLockShared(&g_lock);
    const std::uint32_t result =
        g_ready.load(std::memory_order_acquire) && g_bridge.serverRealTime != nullptr
            ? g_bridge.serverRealTime(g_bridge.utils)
            : 0;
    ReleaseSRWLockShared(&g_lock);
    if (result != 0
        && !g_serverRealTimeReported.exchange(true, std::memory_order_acq_rel)) {
        std::array<char, 128> line{};
        const int written = std::snprintf(line.data(),
                                          line.size(),
                                          "ev=steam_transport stage=server_real_time result=ok value=%lu",
                                          static_cast<unsigned long>(result));
        if (written > 0) {
            core::log::write(
                core::log::Channel::client,
                core::log::Level::info,
                {line.data(),
                 (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
        }
    }
    return result;
}

ApiCall request_certificate() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    ApiCall result = 0;
    if (g_ready.load(std::memory_order_acquire) && g_bridge.certificate != nullptr) {
        const ApiCall candidate = g_bridge.certificate(g_bridge.serialized);
        if (candidate != 0 && add_pending(candidate)) {
            result = candidate;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

int network_config(void* output, DWORD capacity, const char* launcherPartner) noexcept {
    AcquireSRWLockShared(&g_lock);
    const int result = g_ready.load(std::memory_order_acquire)
                               && g_bridge.networkConfig != nullptr
                           ? g_bridge.networkConfig(
                                 g_bridge.serialized, output, capacity, launcherPartner)
                           : 0;
    ReleaseSRWLockShared(&g_lock);
    return result;
}

void cache_relay_ticket(const void* ticket, DWORD ticketSize) noexcept {
    AcquireSRWLockShared(&g_lock);
    if (g_ready.load(std::memory_order_acquire) && g_bridge.cacheRelayTicket != nullptr) {
        g_bridge.cacheRelayTicket(g_bridge.serialized, ticket, ticketSize);
    }
    ReleaseSRWLockShared(&g_lock);
}

DWORD relay_ticket_count() noexcept {
    AcquireSRWLockShared(&g_lock);
    const DWORD result = g_ready.load(std::memory_order_acquire)
                                 && g_bridge.relayTicketCount != nullptr
                             ? g_bridge.relayTicketCount(g_bridge.serialized)
                             : 0;
    ReleaseSRWLockShared(&g_lock);
    return result;
}

int relay_ticket(DWORD index, void* output, DWORD capacity) noexcept {
    AcquireSRWLockShared(&g_lock);
    const int result = g_ready.load(std::memory_order_acquire)
                               && g_bridge.relayTicket != nullptr
                           ? g_bridge.relayTicket(g_bridge.serialized, index, output, capacity)
                           : 0;
    ReleaseSRWLockShared(&g_lock);
    return result;
}

void post_connection_state(const void* message, DWORD messageSize) noexcept {
    AcquireSRWLockShared(&g_lock);
    if (g_ready.load(std::memory_order_acquire) && g_bridge.postConnectionState != nullptr) {
        g_bridge.postConnectionState(g_bridge.serialized, message, messageSize);
    }
    ReleaseSRWLockShared(&g_lock);
}

void service() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    if (!g_ready.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_lock);
        return;
    }
    if (!g_reported) {
        core::log::write(core::log::Channel::client,
                         core::log::Level::info,
                         "ev=steam_transport stage=initialize result=ok identity=valve_bound");
        g_reported = true;
    }
    g_bridge.dispatchFrame(g_bridge.pipe);
    CallbackMessage callback{};
    std::size_t delivered = 0;
    while (delivered < kMaximumCallbacksPerPump
           && g_bridge.dispatchNext(g_bridge.pipe, &callback)) {
        if (callback.callback == kApiCallCompletedCallback
            && callback.parameter != nullptr
            && callback.parameterBytes >= static_cast<int>(sizeof(ApiCallCompleted))) {
            ApiCallCompleted completed{};
            std::memcpy(&completed, callback.parameter, sizeof(completed));
            const bool certificateCall = remove_pending(completed.call);
            const bool registeredCall = callbacks::has_call_result(completed.call);
            if ((certificateCall || registeredCall)
                && completed.parameterBytes <= kMaximumCallbackPayload) {
                std::array<std::byte, kMaximumCallbackPayload> payload{};
                bool failed = true;
                const bool received = g_bridge.dispatchResult(
                    g_bridge.pipe,
                    completed.call,
                    payload.data(),
                    static_cast<int>(completed.parameterBytes),
                    completed.callback,
                    &failed);
                const bool queued = received && !failed
                                    && queue_callback(completed.callback,
                                                      completed.call,
                                                      payload.data(),
                                                      completed.parameterBytes);
                if (received && !failed && certificateCall && completed.callback == 1296) {
                    native_certificate_publish::capture(
                        std::span(payload).first(completed.parameterBytes), platform_id());
                }
                if (received && !failed && completed.callback == 2101
                    && completed.parameterBytes >= 28) {
                    std::uint32_t request{};
                    std::uint8_t successful{};
                    std::int32_t status{};
                    std::uint32_t bodyBytes{};
                    std::memcpy(&request, payload.data(), sizeof(request));
                    std::memcpy(&successful, payload.data() + 16, sizeof(successful));
                    std::memcpy(&status, payload.data() + 20, sizeof(status));
                    std::memcpy(&bodyBytes, payload.data() + 24, sizeof(bodyBytes));
                    std::array<char, 192> httpLine{};
                    const int httpWritten = std::snprintf(
                        httpLine.data(),
                        httpLine.size(),
                        "ev=steam_transport stage=sdr_http result=completed request=%lu success=%u status=%ld body_bytes=%lu",
                        static_cast<unsigned long>(request),
                        static_cast<unsigned>(successful),
                        static_cast<long>(status),
                        static_cast<unsigned long>(bodyBytes));
                    if (httpWritten > 0) {
                        core::log::write(
                            core::log::Channel::client,
                            core::log::Level::info,
                            {httpLine.data(),
                             (std::min)(static_cast<std::size_t>(httpWritten),
                                        httpLine.size() - 1U)});
                    }
                }
                std::array<char, 224> line{};
                const int written = std::snprintf(
                    line.data(),
                    line.size(),
                    "ev=steam_transport stage=%s result=%s callback=%d bytes=%u",
                    certificateCall ? "certificate" : "call_result",
                    queued ? "queued" : "bridge_failed",
                    completed.callback,
                    completed.parameterBytes);
                if (written > 0) {
                    core::log::write(
                        core::log::Channel::client,
                        queued ? core::log::Level::info : core::log::Level::error,
                        {line.data(),
                         (std::min)(static_cast<std::size_t>(written), line.size() - 1U)});
                }
            }
        } else if ((callback.callback >= kNetworkingCallbackFirst
                    && callback.callback <= kNetworkingCallbackLast)
                   || (callback.callback >= kHttpCallbackFirst
                       && callback.callback <= kHttpCallbackLast)) {
            if (callback.parameterBytes >= 0
                && callback.parameterBytes <= static_cast<int>(kMaximumCallbackPayload)) {
                (void)queue_callback(callback.callback,
                                     0,
                                     callback.parameter,
                                     static_cast<std::size_t>(callback.parameterBytes));
            }
        }
        g_bridge.dispatchFree(g_bridge.pipe);
        ++delivered;
        callback = {};
    }
    ReleaseSRWLockExclusive(&g_lock);
    native_certificate_publish::service(GetTickCount64());
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    clear_bridge();
    g_reported = false;
    ReleaseSRWLockExclusive(&g_lock);
    native_certificate_publish::reset();
}

} // namespace dawn::steam::runtime::transport
