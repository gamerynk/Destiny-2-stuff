#include "../parser.h"

namespace dawn::core::settings::parser {

bool Parser::client_settings(client::Settings& output) noexcept {
    if (!consume('{')) {
        return false;
    }
    client::Settings candidate = output;
    bool hasUserInterface = false;
    bool hasExternalServer = false;
    if (consume('}')) {
        return true;
    }
    for (;;) {
        std::string_view key;
        if (!string(key) || !consume(':')) {
            return false;
        }
        if (key == "ui") {
            if (hasUserInterface || !client_ui_settings(candidate.userInterface)) {
                return false;
            }
            hasUserInterface = true;
        } else if (key == "external_server") {
            if (hasExternalServer || !client_external_settings(candidate.externalServer)) {
                return false;
            }
            hasExternalServer = true;
        } else if (!skip_value(0)) {
            return false;
        }
        if (consume('}')) {
            output = candidate;
            return true;
        }
        if (!consume(',')) {
            return false;
        }
    }
}

} // namespace dawn::core::settings::parser
