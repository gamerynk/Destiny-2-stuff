#include "ui_notice_overlay.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <imgui.h>

#include "../animation/transition/ui_transition_animation.h"
#include "../scaling/dpi/ui_dpi_scaling.h"

namespace dawn::core::ui::notice {
namespace {

constexpr std::size_t kNoticeCapacity = 4;
constexpr float kNoticeSeconds = 12.0F;
constexpr float kExpired = 0.0F;
constexpr ImGuiID kOverlayAnimationId = 3;
constexpr animation::transition::Rates kVisibilityRates{16.0F, 14.0F};
constexpr float kHiddenProgress = 0.0F;
constexpr float kHalfExtent = 2.0F;
constexpr float kViewportMargin = 24.0F;
constexpr ImVec2 kTopCenterPivot{0.5F, 0.0F};
constexpr char kFailureHeading[] = "Restoration reported a problem";
constexpr char kInformationHeading[] = "Fireteam invitation";
constexpr ImGuiWindowFlags kOverlayFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
    | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize
    | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove;

struct Entry {
    std::array<char, kTextCapacity> text{};
    std::size_t length{};
    float remainingSeconds{};
    bool information{};
};

SRWLOCK g_lock{SRWLOCK_INIT};
std::array<Entry, kNoticeCapacity> g_entries{};

[[nodiscard]] Entry& claim_slot() noexcept {
    Entry* oldest = &g_entries.front();
    for (Entry& entry : g_entries) {
        if (entry.remainingSeconds <= kExpired) {
            return entry;
        }
        if (entry.remainingSeconds < oldest->remainingSeconds) {
            oldest = &entry;
        }
    }
    return *oldest;
}

} // namespace

void raise_kind(std::string_view text, bool information) noexcept {
    if (text.empty()) {
        return;
    }
    if (TryAcquireSRWLockExclusive(&g_lock) == FALSE) {
        // a fault handler must not wait behind the render thread, so the notice is dropped
        return;
    }
    Entry& entry = claim_slot();
    entry.length = (std::min)(text.size(), kTextCapacity);
    std::copy_n(text.begin(), entry.length, entry.text.begin());
    entry.remainingSeconds = kNoticeSeconds;
    entry.information = information;
    ReleaseSRWLockExclusive(&g_lock);
}

void raise(std::string_view text) noexcept {
    raise_kind(text, false);
}

void raise_information(std::string_view text) noexcept {
    raise_kind(text, true);
}

bool draw() noexcept {
    std::array<Entry, kNoticeCapacity> pending{};
    std::size_t pendingCount = 0;
    const float deltaSeconds = ImGui::GetIO().DeltaTime;

    AcquireSRWLockExclusive(&g_lock);
    for (Entry& entry : g_entries) {
        if (entry.remainingSeconds <= kExpired) {
            continue;
        }
        entry.remainingSeconds = (std::max)(entry.remainingSeconds - deltaSeconds, kExpired);
        pending[pendingCount] = entry;
        ++pendingCount;
    }
    ReleaseSRWLockExclusive(&g_lock);

    const float progress = animation::transition::update(kOverlayAnimationId,
                                                         animation::transition::Lane::visibility,
                                                         pendingCount != 0,
                                                         kVisibilityRates,
                                                         kHiddenProgress);
    if (progress <= kHiddenProgress) {
        return false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return false;
    }

    const float margin = scaling::dpi::pixels(kViewportMargin);
    const ImVec2 position{viewport->WorkPos.x + (viewport->WorkSize.x / kHalfExtent),
                          viewport->WorkPos.y + margin};
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, kTopCenterPivot);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, progress);
    if (ImGui::Begin("##restoration_notice", nullptr, kOverlayFlags)) {
        for (std::size_t index = 0; index < pendingCount; ++index) {
            const Entry& entry = pending[index];
            ImGui::TextUnformatted(entry.information ? kInformationHeading : kFailureHeading);
            const char* first = entry.text.data();
            ImGui::TextUnformatted(first, first + entry.length);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return true;
}

} // namespace dawn::core::ui::notice
