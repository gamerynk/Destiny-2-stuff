#pragma once

#include "../../../middleware/content/packages/reader/reader.h"

namespace dawn::client::content::spawn_sets {

[[nodiscard]] bool build(const middleware::content::packages::reader::Source& source,
                         middleware::content::packages::reader::Scratch& scratch) noexcept;

} // namespace dawn::client::content::spawn_sets
