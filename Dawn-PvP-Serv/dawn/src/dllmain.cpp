#include <Windows.h>

#include <intrin.h>

#include "client/runtime/network_route_policy.h"
#include "core/runtime/core_runtime.h"
#include "steam/runtime/internal.h"
#include "steam/runtime/runtime.h"

namespace {

HMODULE g_module{};

}

extern "C" __declspec(dllexport) bool RestorationInitialize() noexcept {
    if (!dawn::client::runtime::network_route_policy::prepare()
        || !dawn::core::initialize(g_module)
        || !dawn::client::runtime::network_route_policy::initialize()) {
        return false;
    }
    // one of two entry points; the guard reports once, whichever ran first
    dawn::client::runtime::network_route_policy::report();
    return true;
}

extern "C" __declspec(dllexport) bool RestorationActivateClient() noexcept {
    return dawn::client::runtime::network_route_policy::ready()
           && dawn::steam::runtime::activate_main_once();
}

extern "C" __declspec(dllexport) bool RestorationShutdown() noexcept {
    return dawn::steam::shutdown();
}

extern "C" __declspec(dllexport) bool SteamAPI_Init() noexcept {
    return dawn::steam::initialize(g_module);
}

extern "C" __declspec(dllexport) void SteamAPI_Shutdown() noexcept {
    (void)dawn::steam::shutdown();
}

extern "C" __declspec(dllexport) void SteamAPI_RunCallbacks() noexcept {
    dawn::steam::run_callbacks();
}

extern "C" __declspec(dllexport) bool SteamAPI_RestartAppIfNecessary(DWORD appId) noexcept {
    dawn::steam::set_app_id(appId);
    return false;
}

extern "C" __declspec(dllexport) bool SteamAPI_IsSteamRunning() noexcept {
    return dawn::steam::is_running();
}

extern "C" __declspec(dllexport) dawn::steam::UserHandle SteamAPI_GetHSteamUser() noexcept {
    return dawn::steam::user_handle();
}

extern "C" __declspec(dllexport) dawn::steam::PipeHandle SteamAPI_GetHSteamPipe() noexcept {
    return dawn::steam::pipe_handle();
}

extern "C" __declspec(dllexport) dawn::steam::UserHandle GetHSteamUser() noexcept {
    return dawn::steam::user_handle();
}

extern "C" __declspec(dllexport) dawn::steam::PipeHandle GetHSteamPipe() noexcept {
    return dawn::steam::pipe_handle();
}

extern "C" __declspec(dllexport) dawn::steam::UserHandle Steam_GetHSteamUserCurrent() noexcept {
    return dawn::steam::user_handle();
}

extern "C" __declspec(dllexport) void SteamAPI_RegisterCallback(void* callback,
                                                                int callbackId) noexcept {
    dawn::steam::register_callback(callback, callbackId);
}

extern "C" __declspec(dllexport) void SteamAPI_UnregisterCallback(void* callback) noexcept {
    dawn::steam::unregister_callback(callback);
}

extern "C" __declspec(dllexport) void
SteamAPI_RegisterCallResult(void* callback, dawn::steam::ApiCall call) noexcept {
    dawn::steam::register_call_result(callback, call);
}

extern "C" __declspec(dllexport) void
SteamAPI_UnregisterCallResult(void* callback, dawn::steam::ApiCall call) noexcept {
    dawn::steam::unregister_call_result(callback, call);
}

extern "C" __declspec(dllexport) void* SteamInternal_ContextInit(void* data) noexcept {
    return dawn::steam::context_init(data);
}

extern "C" __declspec(dllexport) void* SteamInternal_CreateInterface(const char* version) noexcept {
    return dawn::steam::create_interface(version, _ReturnAddress());
}

extern "C" __declspec(dllexport) void* SteamClient() noexcept {
    return dawn::steam::create_interface("SteamClient018", _ReturnAddress());
}

extern "C" __declspec(dllexport) void*
SteamInternal_FindOrCreateUserInterface(dawn::steam::UserHandle user,
                                        const char* version) noexcept {
    return dawn::steam::find_or_create_user_interface(user, version);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_module = nullptr;
    }
    return TRUE;
}
