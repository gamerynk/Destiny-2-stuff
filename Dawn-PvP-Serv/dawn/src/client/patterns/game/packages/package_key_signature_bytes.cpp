#include "package_key_signature_bytes.h"

namespace dawn::client::patterns::game::packages {

// the load is followed by a fixed stack-adjust and tail branch, which makes it unique
constinit const std::array<patterns::PatternByte, kKeyTablePatternSize> kKeyTable =
    signature<kKeyTablePatternSize>(kKeyTableText);

} // namespace dawn::client::patterns::game::packages
