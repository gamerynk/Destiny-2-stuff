#include <Windows.h>

#include <string_view>

#include "../registry/ui_module_registry.h"
#include "../ui_module_descriptor.h"
#include "internal.h"
#include "logs.h"

namespace dawn::core::ui::modules::logs {
namespace {

constexpr std::string_view kStableId = "core.logs";
constexpr std::string_view kDisplayName = "Logs";

registry::PageRegistration g_page;

} // namespace

bool initialize() noexcept {
    return g_page.acquire(Owner::core, kStableId, kDisplayName, &internal::draw, &internal::reset);
}

void shutdown() noexcept {
    g_page.release(&internal::reset);
}

void dispatch_pending_copy(HWND owner) noexcept {
    internal::dispatch_pending_copy(owner);
}

} // namespace dawn::core::ui::modules::logs
