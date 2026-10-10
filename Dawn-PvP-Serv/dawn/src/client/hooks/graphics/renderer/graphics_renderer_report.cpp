#include "graphics_renderer_report.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "../../../../core/logging/log.h"

namespace dawn::client::hooks::graphics::renderer::report {
namespace {

constexpr std::array<std::string_view, 6> kStageNames{
    "none",
    "acquire",
    "target",
    "init",
    "frame",
    "shutdown",
};

/** One name per Reason value, in Reason order. One per line to match the enum. */
// clang-format off
constexpr std::array<std::string_view, 19> kReasonNames{
    "none",
    "window",
    "window_thread",
    "window_class",
    "description",
    "device",
    "context",
    "device_removed",
    "back_buffer",
    "view",
    "view_format",
    "layout",
    "win32_backend",
    "dx11_backend",
    "window_input",
    "font_scale",
    "device_lost",
    "rebuild_target",
    "surface_lost",
};
// clang-format on

static_assert(kStageNames.size() == static_cast<std::size_t>(Stage::shutdown) + 1);
static_assert(kReasonNames.size() == static_cast<std::size_t>(Reason::surfaceLost) + 1);

// a reason never belongs to two stages, so its own bit identifies the whole outcome
static_assert(kReasonNames.size() <= std::numeric_limits<std::uint32_t>::digits);

std::atomic_uint32_t g_writtenReasons{};
std::atomic_bool g_writtenActive{};

[[nodiscard]] std::uint32_t bit_of(Reason reason) noexcept {
    return 1U << static_cast<unsigned>(reason);
}

} // namespace

void note(Stage stage, Reason reason) noexcept {
    const std::uint32_t bit = bit_of(reason);
    if ((g_writtenReasons.fetch_or(bit, std::memory_order_acq_rel) & bit) != 0) {
        return;
    }
    g_writtenActive.store(false, std::memory_order_release);
    const std::string_view stageName = kStageNames[static_cast<std::size_t>(stage)];
    const std::string_view reasonName = kReasonNames[static_cast<std::size_t>(reason)];
    std::array<char, 96> line{};
    const int written = std::snprintf(line.data(),
                                      line.size(),
                                      "ev=renderer stage=%.*s result=fail reason=%.*s",
                                      static_cast<int>(stageName.size()),
                                      stageName.data(),
                                      static_cast<int>(reasonName.size()),
                                      reasonName.data());
    if (written <= 0) {
        return;
    }
    core::log::write(core::log::Channel::client,
                     core::log::Level::warn,
                     {line.data(), static_cast<std::size_t>(written)});
}

void note_active() noexcept {
    g_writtenReasons.store(0, std::memory_order_release);
    if (g_writtenActive.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    core::log::write(
        core::log::Channel::client, core::log::Level::info, "ev=renderer stage=init result=ok");
}

} // namespace dawn::client::hooks::graphics::renderer::report
