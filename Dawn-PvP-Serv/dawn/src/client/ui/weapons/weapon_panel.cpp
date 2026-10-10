#include "weapon_panel.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include "weapon_catalog.h"
#include "weapon_acquire_client.h"

namespace dawn::client::ui::weapons {
namespace {
std::array<char, 128> g_search{};
std::array<char, 128> g_perkSearch{};
std::uint32_t g_selected{};
std::array<std::uint32_t, 12> g_plugs{};
bool g_started{};
bool matches(std::string_view text, std::string_view search) noexcept {
    return std::search(text.begin(), text.end(), search.begin(), search.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    }) != text.end();
}
}
void draw() noexcept {
    if (!g_started) { catalog::load(); g_started = true; }
    const auto view = catalog::snapshot();
    const auto request = acquire_client::snapshot();
    const bool busy = request.status == acquire_client::Status::requesting;
    ImGui::TextUnformatted("Weapons");
    ImGui::Separator();
    ImGui::TextWrapped("Choose a weapon and its supported perks, then acquire it into your current character's inventory.");
    if (view->loading) { ImGui::TextUnformatted("Loading installed weapons and perks..."); return; }
    if (!view->error.empty()) {
        ImGui::TextWrapped("%s", view->error.c_str());
        if (ImGui::Button("Retry")) catalog::load();
        return;
    }
    ImGui::Spacing();
    ImGui::BeginDisabled(busy);
    ImGui::InputTextWithHint("##weapon_search", "Search weapons", g_search.data(), g_search.size());
    const catalog::Weapon* selected{};
    for (const auto& weapon : view->weapons) if (weapon.hash == g_selected) { selected = &weapon; break; }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##weapon", selected ? selected->name.c_str() : "Choose a weapon")) {
        std::size_t results{};
        for (const auto& weapon : view->weapons) {
            if (!matches(weapon.name, g_search.data()) && !matches(weapon.type, g_search.data())) continue;
            ++results;
            ImGui::PushID(static_cast<int>(weapon.hash));
            if (ImGui::Selectable(weapon.name.c_str(), weapon.hash == g_selected)) {
                g_selected = weapon.hash; g_plugs = weapon.defaults; selected = &weapon;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(weapon.type.c_str());
                if (!weapon.description.empty()) {
                    ImGui::PushTextWrapPos(350);
                    ImGui::TextUnformatted(weapon.description.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
        if (!results) ImGui::TextDisabled("No weapons match your search.");
        ImGui::EndCombo();
    }
    if (selected) {
        if (!selected->type.empty()) ImGui::TextDisabled("%s", selected->type.c_str());
        ImGui::Spacing();
        ImGui::InputTextWithHint("##perk_search", "Search perks", g_perkSearch.data(), g_perkSearch.size());
        for (std::size_t lane = 0; lane < selected->laneCount; ++lane) {
            const auto& options = *selected->lanes[lane];
            const char* name = g_plugs[lane] ? "Default perk" : "Empty";
            for (const auto& option : options) if (option.hash == g_plugs[lane]) { name = option.name.c_str(); break; }
            ImGui::PushID(static_cast<int>(lane));
            ImGui::AlignTextToFramePadding(); ImGui::Text("Socket %zu", lane + 1);
            ImGui::SameLine(90); ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##plug", name)) {
                if (ImGui::Selectable("Use default", g_plugs[lane] == selected->defaults[lane])) g_plugs[lane] = selected->defaults[lane];
                for (const auto& option : options) {
                    if (!matches(option.name, g_perkSearch.data())) continue;
                    ImGui::PushID(static_cast<int>(option.hash));
                    if (ImGui::Selectable(option.name.c_str(), option.hash == g_plugs[lane])) g_plugs[lane] = option.hash;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (ImGui::Button("Acquire")) {
            (void)acquire_client::acquire(0, selected->hash,
                std::span<const std::uint32_t>(g_plugs.data(), selected->laneCount));
        }
    }
    ImGui::EndDisabled();
    if (busy) ImGui::TextUnformatted("Acquiring weapon...");
    else if (request.status == acquire_client::Status::acquired) ImGui::TextWrapped("Acquired. Your inventory is updating.");
    else if (request.status == acquire_client::Status::failed) ImGui::TextWrapped("%s", request.error.data());
}
void shutdown() noexcept { catalog::shutdown(); acquire_client::shutdown(); }
}
