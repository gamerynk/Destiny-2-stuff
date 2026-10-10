#include <limits>

#include "../../state/activity/defaults/activity_defaults_validation.h"
#include "../../state/entitlements/validation.h"
#include "parser.h"

namespace dawn::core::settings::parser {

Parser::Parser(std::string_view input) noexcept : input_(input) {}

bool Parser::parse_root(Settings& output) noexcept {
    if (!consume('{')) {
        return false;
    }
    if (consume('}')) {
        return at_end();
    }
    bool hasVersion = false;
    bool hasCore = false;
    bool hasClient = false;
    bool hasServer = false;
    bool hasSteam = false;
    bool hasState = false;
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "version") {
            std::uint64_t value{};
            if (hasVersion || !unsigned_integer(value)
                || value > (std::numeric_limits<std::uint32_t>::max)()) {
                return false;
            }
            output.version = static_cast<std::uint32_t>(value);
            hasVersion = true;
        } else if (key == "core") {
            if (hasCore || !core(output)) {
                return false;
            }
            hasCore = true;
        } else if (key == "client") {
            if (hasClient || !client_settings(output.client)) {
                return false;
            }
            hasClient = true;
        } else if (key == "server") {
            if (hasServer || !server_settings(output.server)) {
                return false;
            }
            hasServer = true;
        } else if (key == "steam") {
            if (hasSteam || !steam_settings(output.steam)) {
                return false;
            }
            hasSteam = true;
        } else if (key == "state") {
            if (hasState || !state_settings(output)) {
                return false;
            }
            hasState = true;
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            return at_end();
        }
        if (!consume(',')) {
            return false;
        }
    }
}

bool Parser::core(Settings& output) noexcept {
    if (!consume('{')) {
        return false;
    }
    if (consume('}')) {
        return true;
    }
    bool hasLogging = false;
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "logging") {
            if (hasLogging || !logging(output.logging)) {
                return false;
            }
            hasLogging = true;
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

bool Parser::logging(log::Settings& output) noexcept {
    if (!consume('{')) {
        return false;
    }
    if (consume('}')) {
        return true;
    }
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "debugger_sink") {
            if (!boolean(output.debuggerSink)) {
                return false;
            }
        } else if (key == "file_sink") {
            if (!boolean(output.fileSink)) {
                return false;
            }
        } else if (key == "levels") {
            if (!levels(output)) {
                return false;
            }
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

bool Parser::levels(log::Settings& output) noexcept {
    if (!consume('{')) {
        return false;
    }
    if (consume('}')) {
        return true;
    }
    for (;;) {
        std::string_view channelName;
        std::string_view levelName;
        if (!string(channelName) || !consume(':') || !string(levelName)) {
            return false;
        }

        log::Level level{};
        if (!level_value(levelName, level)) {
            return false;
        }
        const std::size_t index = channel_index(channelName);
        if (index < output.levels.size()) {
            output.levels[index] = level;
        }

        if (consume('}')) {
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

} // namespace dawn::core::settings::parser

namespace dawn::core::settings {

Settings defaults() noexcept {
    return Settings{
        .version = kSettingsVersion,
        .logging = log::defaults(),
        .server = server::Settings{state::entitlements::authored()},
        .initialActivityDefaults = state::activity::defaults::authored(),
    };
}

bool parse(std::string_view json, Settings& output) noexcept {
    Settings parsed = defaults();
    parser::Parser parser(json);
    if (!parser.parse_root(parsed)) {
        return false;
    }
    output = parsed;
    return true;
}

} // namespace dawn::core::settings
