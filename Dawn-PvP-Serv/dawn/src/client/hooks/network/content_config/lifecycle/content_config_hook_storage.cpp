#include "../internal.h"
namespace dawn::client::hooks::network::content_config {
SRWLOCK g_lifecycleLock{SRWLOCK_INIT};
gate::Ownership g_gateOwnership{};
bool g_accepting{};
native_work::Metadata g_nativeWork{};
}
