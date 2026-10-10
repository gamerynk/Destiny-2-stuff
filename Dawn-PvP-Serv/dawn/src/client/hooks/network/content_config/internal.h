#pragma once
#include <Windows.h>
#include "gate/content_config_signature_gate.h"
#include "content_native_quiescence.h"
namespace dawn::client::hooks::network::content_config {
extern SRWLOCK g_lifecycleLock;
extern gate::Ownership g_gateOwnership;
extern bool g_accepting;
extern native_work::Metadata g_nativeWork;
}
