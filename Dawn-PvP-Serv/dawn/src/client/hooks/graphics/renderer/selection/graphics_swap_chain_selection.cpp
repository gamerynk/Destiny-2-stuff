#include <Windows.h>

#include <bit>
#include <cstdint>

#include "../../graphics_hook_replacements.h"
#include "../graphics_renderer_report.h"
#include "../state.h"

namespace dawn::client::hooks::graphics::renderer::selection {
namespace {

// 64 px minimum rejects hidden probe windows and short-lived zero-area ones
constexpr std::int64_t kMinimumClientExtent = 64;

[[nodiscard]] bool eligible_window(HWND window, bool reporting) noexcept {
    const auto reject = [reporting](report::Reason reason) noexcept {
        if (reporting) {
            report::note(report::Stage::acquire, reason);
        }
        return false;
    };
    if (window == nullptr || IsWindow(window) == FALSE || IsWindowVisible(window) == FALSE
        || GetAncestor(window, GA_ROOT) != window) {
        return reject(report::Reason::window);
    }

    DWORD processId = 0;
    const DWORD threadId = GetWindowThreadProcessId(window, &processId);
    if (processId != GetCurrentProcessId() || threadId != GetCurrentThreadId()) {
        return reject(report::Reason::windowThread);
    }

    RECT client{};
    if (GetClientRect(window, &client) == FALSE || client.right <= client.left
        || client.bottom <= client.top) {
        return reject(report::Reason::window);
    }
    const std::int64_t width = static_cast<std::int64_t>(client.right) - client.left;
    const std::int64_t height = static_cast<std::int64_t>(client.bottom) - client.top;
    if (width < kMinimumClientExtent || height < kMinimumClientExtent) {
        return reject(report::Reason::window);
    }
    return true;
}

[[nodiscard]] bool main_image_window_class(HWND window, bool reporting) noexcept {
    SetLastError(ERROR_SUCCESS);
    const ULONG_PTR procedure = GetClassLongPtrW(window, GCLP_WNDPROC);
    const bool owned = (procedure != 0 || GetLastError() == ERROR_SUCCESS)
                       && executable_image_target(std::bit_cast<const void*>(procedure),
                                                  GetModuleHandleW(nullptr));
    if (!owned && reporting) {
        report::note(report::Stage::acquire, report::Reason::windowClass);
    }
    return owned;
}

[[nodiscard]] bool
eligible_swap_chain(IDXGISwapChain* swapChain, HWND& window, bool reporting) noexcept {
    if (swapChain == nullptr) {
        return false;
    }
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(swapChain->GetDesc(&description))
        || (description.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT) == 0) {
        if (reporting) {
            report::note(report::Stage::acquire, report::Reason::description);
        }
        return false;
    }
    if (!eligible_window(description.OutputWindow, reporting)
        || !main_image_window_class(description.OutputWindow, reporting)) {
        return false;
    }
    window = description.OutputWindow;
    return true;
}

} // namespace

bool acquire(IDXGISwapChain* swapChain, Resources& output) noexcept {
    output = {};
    HWND window = nullptr;
    if (!eligible_swap_chain(swapChain, window, true)) {
        return false;
    }

    Resources staged;
    staged.window = window;
    const HRESULT deviceResult =
        swapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&staged.device));
    if (FAILED(deviceResult) || staged.device == nullptr) {
        // a D3D12 or D3D11On12 surface fails here
        report::note(report::Stage::acquire, report::Reason::device);
        release_resources(staged);
        return false;
    }
    if (FAILED(staged.device->GetDeviceRemovedReason())) {
        // a removed device never comes back, so refuse the rebuild here rather than at the target
        report::note(report::Stage::acquire, report::Reason::deviceRemoved);
        release_resources(staged);
        return false;
    }
    staged.device->GetImmediateContext(&staged.context);
    if (staged.context == nullptr) {
        report::note(report::Stage::acquire, report::Reason::context);
        release_resources(staged);
        return false;
    }
    swapChain->AddRef();
    staged.swapChain = swapChain;
    if (!kTransientRenderTarget && !create_render_target(staged)) {
        release_resources(staged);
        return false;
    }

    output = staged;
    return true;
}

bool matches_output_window(IDXGISwapChain* swapChain, HWND window) noexcept {
    HWND candidateWindow = nullptr;
    return window != nullptr && eligible_swap_chain(swapChain, candidateWindow, false)
           && candidateWindow == window;
}

} // namespace dawn::client::hooks::graphics::renderer::selection
