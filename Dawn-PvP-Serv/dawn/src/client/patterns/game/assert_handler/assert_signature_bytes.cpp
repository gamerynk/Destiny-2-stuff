#include "assert_signature_bytes.h"

namespace dawn::client::patterns::game::assert_handler {

// stores the argument into the handler slot and returns; the displacement is the slot
constinit const std::array<patterns::PatternByte, kSetHandlerPatternSize> kSetHandler =
    signature<kSetHandlerPatternSize>(kSetHandlerText);

// the report call, then a dword store through address one: the site's deliberate fatal write
constinit const std::array<patterns::PatternByte, kFatalTailPatternSize> kFatalTail =
    signature<kFatalTailPatternSize>(kFatalTailText);

} // namespace dawn::client::patterns::game::assert_handler
