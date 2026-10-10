#pragma once

#include "../../../patterns/image_scan.h"

namespace dawn::client::hooks::network::investment {

using patterns::resolve_relative;
using patterns::scan_main_image_unique;
using patterns::signature;
using patterns::signature_length;

[[nodiscard]] bool install_family5_rearm() noexcept;

[[nodiscard]] bool uninstall_family5_rearm() noexcept;

[[nodiscard]] bool family5_rearm_is_installed() noexcept;

// armed when the family-four lookup first returns a real object and after each family-five
// commit, the first point the account's unlock overrides can be read back
void arm_derived_rebuild() noexcept;

} // namespace dawn::client::hooks::network::investment
