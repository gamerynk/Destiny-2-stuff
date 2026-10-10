#pragma once
#include "mission_script.h"
#include "script_value.h"

namespace dawn::state::activity::coo::script::lua {
[[nodiscard]] value::Value evaluate(std::string_view source,const Profile& profile,std::string_view sourceName);
}
