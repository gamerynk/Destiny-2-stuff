#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include "../dawn/src/client/hooks/graphics/graphics_hook_replacements.cpp"
#include "../dawn/src/client/hooks/graphics/renderer/graphics_renderer_device.cpp"

namespace fixture {
int checks{}, createdTargets{}, shutdowns{}, frames{};
bool failTarget{}, resizeDuringFrame{};
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}
namespace dawn::client::hooks::graphics {
SRWLOCK g_hookLock{SRWLOCK_INIT};
std::atomic_uint g_activeHookCalls{};
thread_local std::size_t g_hookCallDepth{};
bool g_acceptRendererCalls{true};
std::array<hooking::detour::Handle, kHandleCount> g_handles{};
std::array<void*, kHandleCount> g_targetEntries{};
Targets g_targets{};
namespace renderer {
SRWLOCK g_rendererLock{SRWLOCK_INIT};
Resources g_resources{};
bool create_render_target(Resources& resources) noexcept {
    if (fixture::failTarget) return false;
    ID3D11Texture2D* buffer{};
    if (FAILED(resources.swapChain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
    const HRESULT result = resources.device->CreateRenderTargetView(buffer, nullptr, &resources.renderTarget);
    buffer->Release();
    if (SUCCEEDED(result)) ++fixture::createdTargets;
    return SUCCEEDED(result);
}
void release_render_target(Resources& resources) noexcept {
    if (resources.renderTarget) resources.renderTarget->Release();
    resources.renderTarget = nullptr;
}
bool shutdown_locked() noexcept {
    release_render_target(g_resources);
    g_resources = {};
    ++fixture::shutdowns;
    return true;
}
void present(IDXGISwapChain*, ID3D11DeviceContext*) noexcept {}
bool fully_active_locked() noexcept { return true; }
void render_frame_locked() noexcept {
    ++fixture::frames;
    fixture::require(g_resources.renderTarget != nullptr, "draw owns current buffer view");
    if (fixture::resizeDuringFrame) {
        fixture::require(FAILED(g_resources.swapChain->ResizeBuffers(1, 300, 200,
            DXGI_FORMAT_UNKNOWN, 0)), "unserialized native resize cannot proceed during an owned draw view");
    }
}
void dispatch_pending_input_release() noexcept {}
namespace report { void note(Stage, Reason) noexcept {} }
}
}
namespace {
HRESULT STDMETHODCALLTYPE real_resize(IDXGISwapChain* chain, UINT count, UINT w, UINT h, DXGI_FORMAT format, UINT flags) {
    fixture::require(dawn::client::hooks::graphics::renderer::g_resources.renderTarget == nullptr,
                     "native ResizeBuffers sees no owned overlay back-buffer reference");
    return chain->ResizeBuffers(count, w, h, format, flags);
}
HRESULT STDMETHODCALLTYPE real_present(IDXGISwapChain*, UINT, UINT) { return DXGI_ERROR_DEVICE_REMOVED; }
}
int main() {
    using namespace dawn::client::hooks::graphics;
    namespace r = dawn::client::hooks::graphics::renderer;
    const HWND window = CreateWindowExW(0, L"STATIC", L"Restoration offline graphics fixture",
        WS_POPUP, 0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    fixture::require(window != nullptr && !IsWindowVisible(window), "fixture window stays hidden");
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = desc.BufferDesc.Height = 128;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* chain{};
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    fixture::require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context)), "windowed WARP chain created");
    r::g_resources.swapChain = chain;
    r::g_resources.device = device;
    r::g_resources.context = context;
    fixture::require(r::create_render_target(r::g_resources), "initial overlay RTV created");
    ID3D11Texture2D* before{};
    fixture::require(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&before))), "original buffer retrieved");
    const auto* initialTarget = r::g_resources.renderTarget;
    fixture::require(SUCCEEDED(chain->SetFullscreenState(FALSE, nullptr)), "native windowed state method succeeds without hook");
    ID3D11Texture2D* after{};
    fixture::require(SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&after))) && before == after
                     && initialTarget == r::g_resources.renderTarget, "windowed state call retains same buffer and owned RTV");
    before->Release(); after->Release();
    if constexpr (r::kTransientRenderTarget) {
        r::release_render_target(r::g_resources);
        r::render_present_frame_locked();
        fixture::require(r::g_resources.renderTarget == nullptr && fixture::frames == 1,
            "transient draw drops every owned buffer view before original Present");
        fixture::require(SUCCEEDED(chain->ResizeBuffers(1, 144, 160, DXGI_FORMAT_UNKNOWN, 0)),
            "direct unhooked ResizeBuffers succeeds between overlay frames");
        fixture::resizeDuringFrame = true;
        r::render_present_frame_locked();
        fixture::resizeDuringFrame = false;
        fixture::require(r::g_resources.renderTarget == nullptr,
            "failed native resize during draw still leaves no view between frames");
        fixture::failTarget = true;
        const int frames = fixture::frames;
        r::render_present_frame_locked();
        fixture::require(fixture::frames == frames && fixture::shutdowns == 0
            && r::g_resources.swapChain == chain && r::g_resources.renderTarget == nullptr,
            "temporary view failure skips draw and preserves resource/input ownership");
        fixture::failTarget = false;
        r::render_present_frame_locked();
        fixture::require(fixture::frames == frames + 1 && r::g_resources.renderTarget == nullptr,
            "next frame recovers without rebuilding persistent resources");
    }
    g_handles[static_cast<std::size_t>(HookSlot::resizeBuffers)].original = reinterpret_cast<void*>(&real_resize);
    const auto resize = reinterpret_cast<ResizeBuffers>(replacement::entry_points()[1]);
    fixture::require(SUCCEEDED(resize(chain, 1, 256, 192, DXGI_FORMAT_UNKNOWN, 0)), "guarded actual resize succeeds");
    fixture::require((r::kTransientRenderTarget ? r::g_resources.renderTarget == nullptr : r::g_resources.renderTarget != nullptr)
        && r::g_resources.activeSurfaceChanges == 0, "successful resize preserves configured target lifetime");
    fixture::require(SUCCEEDED(chain->GetDesc(&desc)) && desc.BufferDesc.Width == 256 && desc.BufferDesc.Height == 192, "native resized dimensions preserved");
    fixture::require(FAILED(resize(chain, 99, 256, 192, DXGI_FORMAT_UNKNOWN, 0)), "native invalid-buffer-count failure preserved");
    fixture::require((r::kTransientRenderTarget ? r::g_resources.renderTarget == nullptr : r::g_resources.renderTarget != nullptr)
        && r::g_resources.activeSurfaceChanges == 0 && fixture::shutdowns == 0,
                     "failed resize preserves resources and target lifetime over retained native buffer");
    const int created = fixture::createdTargets;
    r::before_surface_change(chain); r::before_surface_change(chain);
    r::after_surface_change(chain, S_OK);
    fixture::require(r::g_resources.renderTarget == nullptr && r::g_resources.activeSurfaceChanges == 1 && fixture::createdTargets == created,
                     "overlapping surface change waits for final native completion");
    r::after_surface_change(chain, S_OK);
    fixture::require(r::kTransientRenderTarget
        ? r::g_resources.renderTarget == nullptr && fixture::createdTargets == created
        : r::g_resources.renderTarget != nullptr && fixture::createdTargets == created+1,
        "overlapping change retains no transient view or creates one persistent final view");
    r::present_result(nullptr, DXGI_ERROR_DEVICE_REMOVED);
    fixture::require(fixture::shutdowns == 0, "foreign swap-chain device loss ignored");
    g_handles[static_cast<std::size_t>(HookSlot::present)].original = reinterpret_cast<void*>(&real_present);
    fixture::require(replacement::present(chain, 0, DXGI_PRESENT_TEST) == DXGI_ERROR_DEVICE_REMOVED, "Present forwards device loss without drawing test frame");
    fixture::require(fixture::shutdowns == 1 && r::g_resources.renderTarget == nullptr, "Present device-loss boundary releases overlay resources");
    context->Release(); chain->Release(); device->Release(); DestroyWindow(window);
    std::printf("PASS %d graphics checks on hidden windowed WARP; fullscreen display untouched\n", fixture::checks);
}
