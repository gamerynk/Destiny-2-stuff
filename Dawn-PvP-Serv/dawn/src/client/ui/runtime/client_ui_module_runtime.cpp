#include "client_ui_module_runtime.h"

#include <string_view>

#include "../../../core/ui/modules/registry/ui_module_registry.h"
#include "../../../core/ui/modules/ui_module_descriptor.h"
#include "../account/account_panel.h"
#include "../weapons/weapon_panel.h"

namespace dawn::client::ui::runtime {
namespace {

constexpr std::string_view kAccountStableId = "client.account";
constexpr std::string_view kAccountDisplayName = "Account";

core::ui::modules::registry::PageRegistration g_accountPage;
core::ui::modules::registry::PageRegistration g_weaponsPage;

} // namespace

bool initialize() noexcept {
    const bool accountOwned = g_accountPage.acquire(
        core::ui::modules::Owner::client, kAccountStableId, kAccountDisplayName, &account::draw);
    const bool weaponsOwned = g_weaponsPage.acquire(
        core::ui::modules::Owner::client, "client.weapons", "Weapons", &weapons::draw);
    return accountOwned && weaponsOwned;
}

void shutdown() noexcept {
    g_weaponsPage.release();
    weapons::shutdown();
    g_accountPage.release();
    account::shutdown();
}

} // namespace dawn::client::ui::runtime
