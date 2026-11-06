// DLL entry, COM exports, self-registration. Registration is the single source of truth
// for the registry layout; scripts/install.ps1 just runs regsvr32 and verifies.
#include <windows.h>

#include <objbase.h>

#include <cstdio>

#include "class_factory.h"
#include "constants.h"
#include "logger.h"
#include "module.h"
#include "veh.h"

namespace {

HMODULE g_module = nullptr;
LONG g_locks = 0;
bool g_stealth = false;

LSTATUS SetString(const wchar_t* key, const wchar_t* name, const wchar_t* value) {
    HKEY h = nullptr;
    LSTATUS s = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr, 0, KEY_WRITE, nullptr, &h, nullptr);
    if (s != ERROR_SUCCESS) return s;
    s = RegSetValueExW(h, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                       static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return s;
}

LSTATUS DeleteTree(const wchar_t* key) {
    const LSTATUS s = RegDeleteTreeW(HKEY_LOCAL_MACHINE, key);
    return s == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : s;
}

void Paths(wchar_t (&clsidKey)[128], wchar_t (&inprocKey)[128], wchar_t (&asioKey)[128]) {
    wchar_t clsid[40];
    StringFromGUID2(cb::CLSID_CrashBarrier, clsid, 40);
    swprintf_s(clsidKey, L"SOFTWARE\\Classes\\CLSID\\%s", clsid);
    swprintf_s(inprocKey, L"%s\\InprocServer32", clsidKey);
    swprintf_s(asioKey, L"SOFTWARE\\ASIO\\%s", cb::kProxyName);
}

}  // namespace

namespace cb {
void ModuleLock() { InterlockedIncrement(&g_locks); }
void ModuleUnlock() { InterlockedDecrement(&g_locks); }
HMODULE ModuleHandle() { return g_module; }
bool Stealth() { return g_stealth; }
}  // namespace cb

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);  // nothing else here: loader lock
    } else if (reason == DLL_PROCESS_DETACH) {
        // Only reached on an orderly ExitProcess/FreeLibrary. TerminateProcess skips it, so a
        // trace without this line ended abruptly.
        cb::log::Line("DllMain: process detach (%s)", reserved ? "process exiting" : "DLL unloaded");
    }
    return TRUE;
}

STDAPI DllCanUnloadNow() { return g_locks == 0 ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv) {
    char c[40], i[40];
    cb::log::Guid(clsid, c, sizeof c);
    cb::log::Guid(riid, i, sizeof i);
    cb::log::Line("DllGetClassObject(clsid=%s iid=%s)", c, i);
    cb::veh::Install();
    if (!ppv) return E_POINTER;
    if (clsid == cb::CLSID_Stadium) {
        g_stealth = true;
        cb::log::Line("activated through the Stadium CLSID: stealth mode");
    } else if (clsid != cb::CLSID_CrashBarrier) {
        *ppv = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return cb::CreateClassFactory(riid, ppv);
}

STDAPI DllRegisterServer() {
    wchar_t clsidKey[128], inprocKey[128], asioKey[128];
    Paths(clsidKey, inprocKey, asioKey);
    wchar_t clsid[40], path[MAX_PATH];
    StringFromGUID2(cb::CLSID_CrashBarrier, clsid, 40);
    GetModuleFileNameW(g_module, path, MAX_PATH);

    LSTATUS s = SetString(clsidKey, nullptr, cb::kProxyName);
    if (s == ERROR_SUCCESS) s = SetString(inprocKey, nullptr, path);
    if (s == ERROR_SUCCESS) s = SetString(inprocKey, L"ThreadingModel", L"Apartment");
    if (s == ERROR_SUCCESS) s = SetString(asioKey, L"CLSID", clsid);
    if (s == ERROR_SUCCESS) s = SetString(asioKey, L"Description", cb::kProxyName);

    char pathA[MAX_PATH * 3];
    cb::log::Utf8(path, pathA, sizeof pathA);
    cb::log::Line("DllRegisterServer: %s -> status %ld", pathA, s);
    return s == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(s);
}

STDAPI DllUnregisterServer() {
    wchar_t clsidKey[128], inprocKey[128], asioKey[128];
    Paths(clsidKey, inprocKey, asioKey);
    LSTATUS s = DeleteTree(asioKey);
    const LSTATUS s2 = DeleteTree(clsidKey);
    if (s == ERROR_SUCCESS) s = s2;
    cb::log::Line("DllUnregisterServer -> status %ld", s);
    return s == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(s);
}
