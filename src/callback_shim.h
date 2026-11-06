// Proxy-owned ASIOCallbacks handed to the real driver. Each entry SEH-guards and
// forwards to the host's original callbacks, so a fault in the host (tagged HOST:)
// is distinguishable from a fault in the driver.
//
// ASIO callbacks carry no context pointer, so this is process-global: exactly one
// owner (the AsioProxy that called createBuffers) may hold it at a time.
#pragma once

#include "asio/iasio.h"

namespace cb::shim {

// nullptr if another owner currently holds the shim.
ASIOCallbacks* Acquire(const void* owner, ASIOCallbacks* host);
void Release(const void* owner);

}  // namespace cb::shim
