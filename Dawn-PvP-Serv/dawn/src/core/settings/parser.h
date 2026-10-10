#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "settings.h"

namespace dawn::core::settings::parser {

class Parser {
public:
    explicit Parser(std::string_view input) noexcept;

    [[nodiscard]] bool parse_root(Settings& output) noexcept;

private:
    [[nodiscard]] bool core(Settings& output) noexcept;
    [[nodiscard]] bool client_settings(client::Settings& output) noexcept;
    [[nodiscard]] bool client_ui_settings(ui::runtime::Settings& output) noexcept;
    [[nodiscard]] bool client_external_settings(client::external::Settings& output) noexcept;
    [[nodiscard]] bool server_settings(server::Settings& output) noexcept;
    [[nodiscard]] bool gameplay_settings(server::gameplay::Settings& output) noexcept;
    // array order is the handle order the Client finds definitions by, so keep it as authored
    [[nodiscard]] bool entitlements(state::entitlements::Table& output) noexcept;
    [[nodiscard]] bool entitlement(state::entitlements::Entitlement& output) noexcept;
    [[nodiscard]] bool steam_settings(steam::Settings& output) noexcept;
    [[nodiscard]] bool steam_user_settings(steam::User& output) noexcept;
    [[nodiscard]] bool state_settings(Settings& output) noexcept;
    [[nodiscard]] bool
    activity_settings(state::activity::defaults::ActivityDefaults& output) noexcept;
    [[nodiscard]] bool
    default_destination(state::activity::defaults::DefaultDestination& output) noexcept;
    [[nodiscard]] bool
    arrival_override(state::activity::defaults::ArrivalOverride& output) noexcept;
    [[nodiscard]] bool
    arrival_overrides(state::activity::defaults::ActivityDefaults& output) noexcept;
    [[nodiscard]] bool unlocks(state::unlocks::Table& output) noexcept;
    [[nodiscard]] bool flag_runs(std::span<std::uint8_t> bank) noexcept;
    [[nodiscard]] bool flag_indices(std::span<std::uint8_t> bank) noexcept;
    [[nodiscard]] bool objective_values(std::span<std::int32_t> bank) noexcept;
    [[nodiscard]] bool progression_values(state::unlocks::ProgressionBank& bank) noexcept;
    [[nodiscard]] bool investment(state::Family5State& output) noexcept;
    [[nodiscard]] bool unlock_flag_overrides(state::Family5State& output) noexcept;
    [[nodiscard]] bool unlock_value_overrides(state::Family5State& output) noexcept;
    [[nodiscard]] bool account(state::AccountState& output) noexcept;
    [[nodiscard]] bool characters(state::AccountState& output) noexcept;
    // a profile item has no authored slot; its inventory bucket picks the first slot
    [[nodiscard]] bool profile_items(state::AccountState& output) noexcept;
    [[nodiscard]] bool dismantle_rewards(state::AccountState& output) noexcept;
    [[nodiscard]] bool character(state::CharacterState& output) noexcept;

    [[nodiscard]] bool ability_entry(std::uint8_t& output) noexcept;
    [[nodiscard]] bool equipment(state::account::inventory::Equipment& output) noexcept;
    [[nodiscard]] bool
    character_inventory(state::account::inventory::CharacterItems& output) noexcept;
    [[nodiscard]] bool equipment_item(state::account::inventory::Item& output) noexcept;
    [[nodiscard]] bool equipment_plugs(state::account::inventory::Sockets& output) noexcept;
    [[nodiscard]] bool inventory_definition_hash(std::uint32_t& output) noexcept;
    [[nodiscard]] bool account_settings(state::account::settings::AccountSettings& output) noexcept;
    [[nodiscard]] bool controls_settings(state::account::settings::Controls& output) noexcept;
    [[nodiscard]] bool audio_settings(state::account::settings::Audio& output) noexcept;
    [[nodiscard]] bool display_settings(state::account::settings::Display& output) noexcept;
    [[nodiscard]] bool interface_settings(state::account::settings::Interface& output) noexcept;
    [[nodiscard]] bool social_settings(state::account::settings::Social& output) noexcept;
    [[nodiscard]] bool
    key_bindings(state::account::settings::bindings::KeyBindings& output) noexcept;
    [[nodiscard]] bool key_binding(state::account::settings::bindings::Binding& output) noexcept;
    [[nodiscard]] bool optional_input_code(std::optional<std::uint16_t>& output) noexcept;
    [[nodiscard]] static bool input_code_value(std::string_view name,
                                               std::uint16_t& output) noexcept;
    [[nodiscard]] bool logging(log::Settings& output) noexcept;
    [[nodiscard]] bool levels(log::Settings& output) noexcept;
    [[nodiscard]] bool skip_value(unsigned depth) noexcept;
    [[nodiscard]] bool string(std::string_view& output) noexcept;
    [[nodiscard]] bool boolean(bool& output) noexcept;
    [[nodiscard]] bool unsigned_integer(std::uint64_t& output) noexcept;
    [[nodiscard]] bool unsigned_value(std::uint64_t& output) noexcept;
    [[nodiscard]] bool signed_integer(std::int64_t& output) noexcept;
    [[nodiscard]] bool signed_byte(std::int8_t& output) noexcept;
    [[nodiscard]] bool signed_32(std::int32_t& output) noexcept;
    [[nodiscard]] bool floating_point(float& output) noexcept;
    [[nodiscard]] bool number() noexcept;
    [[nodiscard]] bool literal(std::string_view value) noexcept;
    [[nodiscard]] bool consume(char value) noexcept;
    [[nodiscard]] bool at_end() noexcept;
    void whitespace() noexcept;
    [[nodiscard]] static bool level_value(std::string_view name, log::Level& output) noexcept;
    [[nodiscard]] static std::size_t channel_index(std::string_view name) noexcept;
    [[nodiscard]] static bool ui_toggle_key_value(std::string_view name, UINT& output) noexcept;

    std::string_view input_;
    std::size_t position_{};
};

} // namespace dawn::core::settings::parser
