// Process-wide observer for hardware exceptions on threads the proxy never calls into
// (driver worker threads, timer callbacks). Logs first-chance access violations with
// module+offset and the faulting thread's start module, writes a dump for the first few,
// then always continues the search so the host's own handling is unchanged.
#pragma once

namespace cb::veh {

// Idempotent. Pins the proxy DLL in the process so the handler can never outlive its code.
void Install();

}  // namespace cb::veh
