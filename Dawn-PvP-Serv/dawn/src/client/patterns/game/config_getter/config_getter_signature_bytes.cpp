#include "config_getter_signature_bytes.h"

namespace dawn::client::patterns::game::config_getter {

// the jump, its stack fixup tail and the two following jumps that make the thunk unique
constinit const std::array<patterns::PatternByte, kUrlGetterPatternSize> kUrlGetter =
    signature<kUrlGetterPatternSize>(kUrlGetterText);

// the jump, the negative-one load its caller tests, and the following jumps
constinit const std::array<patterns::PatternByte, kTokenGetterPatternSize> kTokenGetter =
    signature<kTokenGetterPatternSize>(kTokenGetterText);

} // namespace dawn::client::patterns::game::config_getter
