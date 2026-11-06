// Crash-durable trace log: %LOCALAPPDATA%\CrashBarrier\trace_<pid>_<timestamp>.log
// Every Line() is written and flushed before returning. Control-path only; hot-path
// callers must sample (see callers' counters).
#pragma once

#include <guiddef.h>

#include <cstddef>

namespace cb::log {

void Line(const char* fmt, ...);

// Trace directory (created on demand). Empty string if it could not be created.
const wchar_t* Dir();

// Small conversion helpers for log formatting.
void Utf8(const wchar_t* in, char* out, size_t cap);
void Guid(const GUID& g, char* out, size_t cap);  // "{XXXXXXXX-...}" needs cap >= 39

}  // namespace cb::log
