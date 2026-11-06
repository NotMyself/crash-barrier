// Registry-free load of the Line 6 driver: LoadLibrary -> DllGetClassObject ->
// IClassFactory::CreateInstance with the Stadium CLSID as the IID. Never CoCreateInstance.
#pragma once

#include <windows.h>

#include "asio/iasio.h"

namespace cb {

// Returns the driver object (refcount 1) or nullptr after logging why.
// 'module' receives the loaded HMODULE even when object creation fails.
// Not SEH-guarded: callers wrap it in Guarded().
IASIO* LoadRealDriver(HMODULE& module);

// Thread forensics: snapshot the process's thread ids, later log every thread that did not
// exist in the snapshot, with the module+offset of its start address. Evidence for "the driver
// created threads in init and they are still alive after Release".
struct ThreadSnapshot {
    DWORD ids[512] = {};
    int count = 0;
};
void SnapshotThreads(ThreadSnapshot& out);
void LogNewThreads(const ThreadSnapshot& baseline, const char* when);
void DescribeThreadStart(DWORD tid, char* out, size_t cap);  // "module.dll+0xOFFSET" or "?"

}  // namespace cb
