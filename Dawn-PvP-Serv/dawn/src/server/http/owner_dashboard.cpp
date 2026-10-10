#include "owner_dashboard.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

#include "../../core/settings/settings.h"
#include "../../state/activity/destination/activity_destination_snapshot.h"
#include "../../state/equipment/light/resolution/configured_equipment_light_resolver.h"
#include "../../state/runtime/runtime.h"
#include "../../state/social/social_state.h"
#include "../bap/runtime.h"

namespace dawn::server::http::owner_dashboard {
namespace {

constexpr std::string_view kOrbit = "Orbit";

[[nodiscard]] const char* relationship_name(state::social::Relationship relationship) noexcept {
    switch (relationship) {
    case state::social::Relationship::requestRecipient: return "Incoming request";
    case state::social::Relationship::friendAccount: return "Friend";
    case state::social::Relationship::requestInitiator: return "Outgoing request";
    case state::social::Relationship::blocked: return "Blocked";
    case state::social::Relationship::ignored: return "Ignored";
    case state::social::Relationship::ignoredFriend: return "Ignored friend";
    case state::social::Relationship::none: break;
    }
    return "Unknown";
}

struct Writer final {
    std::span<std::byte> output;
    std::size_t size{};

    [[nodiscard]] bool append(std::string_view text) noexcept {
        if (text.size() > output.size() - size) {
            return false;
        }
        std::memcpy(output.data() + size, text.data(), text.size());
        size += text.size();
        return true;
    }

    [[nodiscard]] bool escaped(std::string_view text) noexcept {
        for (const char value : text) {
            std::string_view replacement(&value, 1);
            switch (value) {
            case '&': replacement = "&amp;"; break;
            case '<': replacement = "&lt;"; break;
            case '>': replacement = "&gt;"; break;
            case '"': replacement = "&quot;"; break;
            case '\'': replacement = "&#39;"; break;
            default: break;
            }
            if (!append(replacement)) {
                return false;
            }
        }
        return true;
    }

    template <typename... Args>
    [[nodiscard]] bool format(const char* pattern, Args... args) noexcept {
        std::array<char, 192> buffer{};
        const int count = std::snprintf(buffer.data(), buffer.size(), pattern, args...);
        return count >= 0 && static_cast<std::size_t>(count) < buffer.size()
               && append({buffer.data(), static_cast<std::size_t>(count)});
    }
};

struct PlayerRow final {
    std::uint64_t accountSoid{};
    std::uint64_t characterSoid{};
    std::uint64_t activitySessionId{};
    std::uint64_t openedAtTick{};
    std::uint64_t lastFrameAtTick{};
    std::array<char, state::activity::destination::kPackageNameCapacity> location{};
    std::size_t locationLength{};
    std::uint32_t links{};
    bool inActivity{};
};

[[nodiscard]] const char* character_class(const state::AccountState& account) noexcept {
    for (const state::CharacterState& character : account.characters) {
        if (!character.selected) {
            continue;
        }
        switch (character.characterClass) {
        case state::CharacterClass::titan: return "Titan";
        case state::CharacterClass::hunter: return "Hunter";
        case state::CharacterClass::warlock: return "Warlock";
        }
    }
    return "No character";
}

[[nodiscard]] std::int32_t selected_power(const state::AccountState& account) noexcept {
    for (std::size_t index = 0; index < account.characterCount; ++index) {
        if (account.characters[index].selected) {
            std::int32_t power = 0;
            return state::equipment::light::resolution::character_light(account, index, power)
                       ? power
                       : 0;
        }
    }
    return 0;
}

[[nodiscard]] PlayerRow* find_or_add(std::span<PlayerRow> rows,
                                     std::size_t& count,
                                     std::uint64_t accountSoid) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (rows[index].accountSoid == accountSoid) {
            return &rows[index];
        }
    }
    if (count == rows.size()) {
        return nullptr;
    }
    PlayerRow& row = rows[count++];
    row = {};
    row.accountSoid = accountSoid;
    return &row;
}

void apply_location(PlayerRow& row, const server::bap::ConnectionStatus& connection) noexcept {
    if (!connection.activityJoined || connection.activitySessionId == 0) {
        return;
    }
    state::activity::destination::DestinationSelection destination{};
    if (!state::activity::destination::snapshot(connection.activitySessionId, destination)) {
        return;
    }
    row.inActivity = true;
    row.activitySessionId = connection.activitySessionId;
    row.locationLength = (std::min)(static_cast<std::size_t>(destination.packageNameLength),
                                    row.location.size());
    for (std::size_t index = 0; index < row.locationLength; ++index) {
        row.location[index] = static_cast<char>(destination.packageName[index]);
    }
}

[[nodiscard]] bool append_duration(Writer& writer, std::uint64_t milliseconds) noexcept {
    const std::uint64_t seconds = milliseconds / 1000;
    if (seconds >= 3600) {
        return writer.format("%lluh %llum",
                             static_cast<unsigned long long>(seconds / 3600),
                             static_cast<unsigned long long>((seconds / 60) % 60));
    }
    if (seconds >= 60) {
        return writer.format("%llum %llus",
                             static_cast<unsigned long long>(seconds / 60),
                             static_cast<unsigned long long>(seconds % 60));
    }
    return writer.format("%llus", static_cast<unsigned long long>(seconds));
}

} // namespace

bool render(std::span<std::byte> output, std::size_t& written) noexcept {
    written = 0;
    std::array<server::bap::ConnectionStatus, client::network::kBapConnectionCount> connections{};
    const std::size_t connectionCount = server::bap::snapshot_connections(connections);
    std::array<PlayerRow, client::network::kBapConnectionCount> rows{};
    std::size_t rowCount = 0;
    for (std::size_t index = 0; index < connectionCount; ++index) {
        const auto& connection = connections[index];
        if (!connection.authenticated || connection.accountSoid == 0) {
            continue;
        }
        PlayerRow* const row = find_or_add(rows, rowCount, connection.accountSoid);
        if (row == nullptr) {
            continue;
        }
        ++row->links;
        row->characterSoid = connection.characterSoid != 0 ? connection.characterSoid
                                                            : row->characterSoid;
        if (row->openedAtTick == 0 || connection.openedAtTick < row->openedAtTick) {
            row->openedAtTick = connection.openedAtTick;
        }
        row->lastFrameAtTick = (std::max)(row->lastFrameAtTick, connection.lastFrameAtTick);
        apply_location(*row, connection);
    }

    const state::AccountState account = state::account_snapshot();
    const state::social::Snapshot social = state::social::snapshot();
    std::size_t incomingRequests = 0;
    for (std::size_t index = 0; index < social.remoteCount; ++index) {
        incomingRequests += social.remotes[index].relationship
                                == state::social::Relationship::requestRecipient;
    }
    const std::uint64_t selectedCharacter = state::account::selected_character_soid(account);
    const std::int32_t power = selected_power(account);
    const char* const className = character_class(account);
    const std::string_view persona = social.localDisplayName.front() != '\0'
                                         ? social.localDisplayName.data()
                                         : core::settings::get().steam.user.personaName.data();
    const std::uint64_t now = GetTickCount64();

    Writer writer{output};
    if (!writer.append(R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><meta http-equiv="refresh" content="2">
<title>Restoration Server</title><style>
:root{color-scheme:dark;--bg:#080b11;--panel:#111722;--line:#263247;--text:#eef3fb;--muted:#91a0b5;--cyan:#59d8e6;--green:#55d98c}*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top left,#163242 0,#080b11 34%);color:var(--text);font:15px/1.45 "Segoe UI",sans-serif;min-height:100vh}.wrap{width:min(1180px,calc(100% - 32px));margin:0 auto;padding:32px 0 50px}header{display:flex;align-items:center;justify-content:space-between;gap:20px;margin-bottom:28px}.brand{display:flex;align-items:center;gap:14px}.mark{display:grid;place-items:center;width:44px;height:44px;border:1px solid #4f7186;background:#152936;color:var(--cyan);font-size:22px;font-weight:700}.eyebrow{color:var(--cyan);font-size:12px;letter-spacing:.2em;text-transform:uppercase}h1{font-size:25px;margin:2px 0 0}.local{color:var(--muted)}.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-bottom:18px}.stat,.panel{background:linear-gradient(145deg,rgba(21,29,43,.96),rgba(12,17,26,.96));border:1px solid var(--line);box-shadow:0 12px 36px #0006}.stat{padding:18px 20px}.stat b{display:block;font-size:28px}.stat span{color:var(--muted)}.panel{overflow:hidden}.panelhead{display:flex;align-items:center;justify-content:space-between;padding:17px 20px;border-bottom:1px solid var(--line)}.panelhead h2{margin:0;font-size:17px}.live{color:var(--green);font-size:13px}.live:before{content:"";display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--green);margin-right:7px;box-shadow:0 0 12px var(--green)}table{border-collapse:collapse;width:100%}th,td{text-align:left;padding:15px 20px;border-bottom:1px solid #1d2635;white-space:nowrap}th{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.08em}td strong{display:block}.sub{color:var(--muted);font-size:12px}.pill{display:inline-block;border:1px solid #326a55;background:#122c24;color:#72e3a6;padding:4px 9px;border-radius:999px}.empty{padding:48px 20px;text-align:center;color:var(--muted)}footer{margin-top:15px;color:var(--muted);font-size:12px}@media(max-width:760px){.stats{grid-template-columns:1fr}.panel{overflow-x:auto}.local{display:none}}
</style></head><body><main class="wrap"><header><div class="brand"><div class="mark">R</div><div><div class="eyebrow">Local owner console</div><h1>Restoration Server</h1></div></div><div class="local">127.0.0.1 · private</div></header><section class="stats"><div class="stat"><b>)HTML")
        || !writer.format("%zu", rowCount)
        || !writer.append(R"HTML(</b><span>Players online</span></div><div class="stat"><b>)HTML")
        || !writer.format("%zu", connectionCount)
        || !writer.append(R"HTML(</b><span>Backend connections</span></div><div class="stat"><b>)HTML")
        || !writer.format("%zu", incomingRequests)
        || !writer.append(R"HTML(</b><span>Incoming friend requests</span></div></section><section class="panel"><div class="panelhead"><h2>Connected players</h2><span class="live">Server online</span></div>)HTML")) {
        return false;
    }

    if (rowCount == 0) {
        if (!writer.append("<div class=\"empty\">No authenticated players are connected.</div>")) {
            return false;
        }
    } else {
        if (!writer.append("<table><thead><tr><th>Player</th><th>Status</th><th>Location</th><th>Power</th><th>Connected</th><th>Last client event</th><th>Links</th></tr></thead><tbody>")) {
            return false;
        }
        for (std::size_t index = 0; index < rowCount; ++index) {
            const PlayerRow& row = rows[index];
            const std::uint64_t character = row.characterSoid != 0 ? row.characterSoid
                                                                   : selectedCharacter;
            const std::string_view location = row.locationLength != 0
                                                  ? std::string_view(row.location.data(),
                                                                     row.locationLength)
                                                  : kOrbit;
            if (!writer.append("<tr><td><strong>") || !writer.escaped(persona)
                || !writer.format("</strong><span class=\"sub\">0x%016llX · %s · character 0x%016llX</span></td><td><span class=\"pill\">%s</span></td><td>",
                                  static_cast<unsigned long long>(row.accountSoid),
                                  className,
                                  static_cast<unsigned long long>(character),
                                  row.inActivity ? "In activity" : "In orbit")
                || !writer.escaped(location)
                || !writer.format("</td><td>%d</td><td>", power)
                || !append_duration(writer,
                                    now >= row.openedAtTick ? now - row.openedAtTick : 0)
                || !writer.append("</td><td>")
                || !append_duration(writer,
                                    now >= row.lastFrameAtTick ? now - row.lastFrameAtTick : 0)
                || !writer.format(" ago</td><td>%u</td></tr>", row.links)) {
                return false;
            }
        }
        if (!writer.append("</tbody></table>")) {
            return false;
        }
    }
    if (!writer.append("</section><section class=\"panel\" style=\"margin-top:18px\"><div class=\"panelhead\"><h2>Social relationships</h2><span class=\"sub\">refreshes every 2 seconds</span></div>")) {
        return false;
    }
    if (social.remoteCount == 0) {
        if (!writer.append("<div class=\"empty\">No friend relationships are stored.</div>")) {
            return false;
        }
    } else {
        if (!writer.append("<table><thead><tr><th>Account</th><th>Relationship</th><th>Presence</th><th>Platform identity</th></tr></thead><tbody>")) {
            return false;
        }
        for (std::size_t index = 0; index < social.remoteCount; ++index) {
            const state::social::RemoteAccount& remote = social.remotes[index];
            const std::string_view name(remote.displayName.data());
            const std::string_view location(remote.location.data());
            if (!writer.append("<tr><td><strong>") || !writer.escaped(name)
                || !writer.format("</strong><span class=\"sub\">account 0x%016llX</span></td><td><span class=\"pill\">%s</span></td><td>",
                                  static_cast<unsigned long long>(remote.accountSoid),
                                  relationship_name(remote.relationship))
                || !writer.escaped(location)
                || !writer.format("</td><td>0x%016llX</td></tr>",
                                  static_cast<unsigned long long>(remote.platformId))) {
                return false;
            }
        }
        if (!writer.append("</tbody></table>")) {
            return false;
        }
    }
    if (!writer.append("</section><footer>Read-only dashboard · served only on this computer</footer></main></body></html>")) {
        return false;
    }
    written = writer.size;
    return true;
}

} // namespace dawn::server::http::owner_dashboard
