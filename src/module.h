// DLL-wide object/lock count (drives DllCanUnloadNow) and our own HMODULE.
#pragma once

#include <windows.h>

namespace cb {

void ModuleLock();
void ModuleUnlock();
HMODULE ModuleHandle();

// Stealth mode: the host activated us through the Stadium's own CLSID (install.ps1 -Stealth), so
// the proxy must look exactly like the real driver (same CLSID-as-IID, same driver name).
bool Stealth();

}  // namespace cb
