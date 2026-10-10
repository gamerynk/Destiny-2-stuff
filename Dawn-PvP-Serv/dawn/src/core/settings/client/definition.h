#pragma once

#include "../../ui/runtime/settings.h"
#include "external/definition.h"

namespace dawn::core::settings::client {

struct Settings {
    ui::runtime::Settings userInterface;
    external::Settings externalServer;
};

} // namespace dawn::core::settings::client
