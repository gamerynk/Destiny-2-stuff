#include "host_environment.h"

#include <Windows.h>

namespace dawn::core::runtime {

bool is_wine() noexcept {
    // only Wine exports this from ntdll; the host can't change, so resolve once
    static const bool underWine = [] {
        const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
    }();
    return underWine;
}

} // namespace dawn::core::runtime
