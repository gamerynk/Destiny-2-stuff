#include <cstdint>
#include <imgui.h>
#include <string_view>

#include "../../../logging/snapshot/snapshot.h"
#include "../../components/toggle/ui_toggle_component.h"
#include "../../scaling/dpi/ui_dpi_scaling.h"
#include "filters/logs_filter_controls.h"
#include "internal.h"

namespace dawn::core::ui::modules::logs::internal {
namespace {

constexpr float kInlineFilterRowWidth = 500.0F;
constexpr float kInlineSelectorRowWidth = 260.0F;
constexpr float kAutoScrollWidth = 160.0F;
constexpr float kInlineStatsRowWidth = 400.0F;
constexpr char kCopyVisibleLabel[] = "Copy Visible";
constexpr char kCopyPendingLabel[] = "Copy queued.";
constexpr char kCopyCompleteLabel[] = "Copied visible lines.";
constexpr char kCopyFailedLabel[] = "Clipboard copy failed.";
constexpr ImVec2 kAutomaticChildSize{0.0F, 0.0F};
constexpr float kScrollBottom = 1.0F;
constexpr ImGuiWindowFlags kLogWindowFlags =
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_HorizontalScrollbar;
constexpr ImGuiChildFlags kLogChildFlags = ImGuiChildFlags_Borders;

bool g_autoScroll{true};
std::uint64_t g_recordedCount{};

// counts replaced entries too: the stored count alone stops rising once the ring wraps
[[nodiscard]] std::uint64_t recorded_count(const log::view::Result& visible) noexcept {
    return visible.overwritten_count() + static_cast<std::uint64_t>(visible.source_count());
}

void draw_copy_button(const log::view::Result& visible) noexcept {
    const float rowMaximumX = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::Button(kCopyVisibleLabel)) {
        (void)request_copy(visible);
    }
    const CopyStatus status = copy_status();
    const char* statusLabel = nullptr;
    if (status == CopyStatus::pending) {
        statusLabel = kCopyPendingLabel;
    } else if (status == CopyStatus::copied) {
        statusLabel = kCopyCompleteLabel;
    } else if (status == CopyStatus::failed) {
        statusLabel = kCopyFailedLabel;
    }
    if (statusLabel == nullptr) {
        return;
    }

    const float availableWidth = rowMaximumX - ImGui::GetItemRectMax().x;
    const ImGuiStyle& style = ImGui::GetStyle();
    if (copy_status_fits_inline(
            availableWidth, style.ItemSpacing.x, ImGui::CalcTextSize(statusLabel).x)) {
        ImGui::SameLine();
    }
    ImGui::TextDisabled("%s", statusLabel);
}

void draw_log_entries(const log::view::Result& visible, bool follow) noexcept {
    if (ImGui::BeginChild("##log_entries", kAutomaticChildSize, kLogChildFlags, kLogWindowFlags)) {
        if (visible.entries().empty()) {
            ImGui::TextDisabled("No retained events match these filters.");
        } else {
            for (const log::snapshot::Entry* entry : visible.entries()) {
                const std::string_view text = entry->text();
                ImGui::TextUnformatted(text.data(), text.data() + text.size());
            }
            // only on a frame that added a record; pinning every frame drags the view down while the
            // user reads older lines
            if (follow) {
                ImGui::SetScrollHereY(kScrollBottom);
            }
        }
    }
    ImGui::EndChild();
}

} // namespace

void draw() noexcept {
    ImGui::TextDisabled("Filter the bounded in-memory history or copy its visible lines.");
    ImGui::Separator();
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool inlineSelectors = availableWidth >= scaling::dpi::pixels(kInlineSelectorRowWidth);
    const bool inlineFilters = availableWidth >= scaling::dpi::pixels(kInlineFilterRowWidth);
    filters::draw_channel();
    if (inlineSelectors) {
        ImGui::SameLine();
    }
    filters::draw_level();
    if (inlineFilters) {
        ImGui::SameLine();
    }
    filters::draw_text();

    const log::snapshot::Snapshot retained = log::snapshot::take();
    const log::view::Result visible = log::view::select(retained, filters::current());
    const std::uint64_t recorded = recorded_count(visible);
    const bool logGrew = recorded != g_recordedCount;
    g_recordedCount = recorded;
    const bool inlineStats =
        ImGui::GetContentRegionAvail().x >= scaling::dpi::pixels(kInlineStatsRowWidth);
    ImGui::Text("Visible: %zu / %zu", visible.entries().size(), visible.source_count());
    if (inlineStats) {
        ImGui::SameLine();
    }
    ImGui::TextDisabled("Replaced: %llu",
                        static_cast<unsigned long long>(visible.overwritten_count()));
    const float autoScrollWidth = scaling::dpi::pixels(kAutoScrollWidth);
    const float copyButtonWidth =
        ImGui::CalcTextSize(kCopyVisibleLabel).x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    const bool inlineActions =
        ImGui::GetContentRegionAvail().x
        >= autoScrollWidth + ImGui::GetStyle().ItemSpacing.x + copyButtonWidth;
    const bool followTurnedOn =
        components::toggle::control("Auto-scroll##log_auto_scroll", g_autoScroll, autoScrollWidth)
        && g_autoScroll;
    if (inlineActions) {
        ImGui::SameLine();
    }
    draw_copy_button(visible);
    ImGui::Separator();
    draw_log_entries(visible, g_autoScroll && (logGrew || followTurnedOn));
}

void reset() noexcept {
    cancel_pending_copy();
    filters::reset();
    g_autoScroll = true;
    g_recordedCount = 0;
}

} // namespace dawn::core::ui::modules::logs::internal
