#include "retail_log_signature_bytes.h"

namespace dawn::client::patterns::game::retail_log {

// site-id guard that opens the enqueue funnel
constinit const std::array<patterns::PatternByte, kEnqueuePatternSize> kEnqueue =
    signature<kEnqueuePatternSize>(kEnqueueText);

// verbosity setter, matched through its category and level argument moves
constinit const std::array<patterns::PatternByte, kSetCategoryVerbosityPatternSize>
    kSetCategoryVerbosity = signature<kSetCategoryVerbosityPatternSize>(kSetCategoryVerbosityText);

} // namespace dawn::client::patterns::game::retail_log
