#pragma once

#include <windows.h>

namespace cb {

// Creates the class factory for CLSID_CrashBarrier and returns the interface 'riid' on it.
HRESULT CreateClassFactory(REFIID riid, void** ppv);

}  // namespace cb
