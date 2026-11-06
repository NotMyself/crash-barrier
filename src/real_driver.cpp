#include "real_driver.h"

#include <tlhelp32.h>
#include <winternl.h>

#include <cstdio>
#include <cwchar>

#include "constants.h"
#include "logger.h"

namespace cb {
namespace {

void LogFileVersion(const wchar_t* path) {
    char pathA[MAX_PATH * 3];
    log::Utf8(path, pathA, sizeof pathA);
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0 || size > 65536) {
        log::Line("real driver: %s (version unavailable)", pathA);
        return;
    }
    BYTE buf[65536];
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT len = 0;
    if (GetFileVersionInfoW(path, 0, size, buf) &&
        VerQueryValueW(buf, L"\\", reinterpret_cast<void**>(&ffi), &len) && ffi) {
        log::Line("real driver: %s v%u.%u.%u.%u", pathA, HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
                  HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    } else {
        log::Line("real driver: %s (version unavailable)", pathA);
    }
}

}  // namespace

IASIO* LoadRealDriver(HMODULE& module) {
    wchar_t override[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(kDriverPathEnv, override, MAX_PATH);
    const wchar_t* target = (n > 0 && n < MAX_PATH) ? override : kRealDllName;
    char targetA[MAX_PATH * 3];
    log::Utf8(target, targetA, sizeof targetA);

    module = LoadLibraryW(target);
    if (!module) {
        log::Line("LoadLibraryW('%s') failed: error %lu", targetA, GetLastError());
        return nullptr;
    }
    wchar_t full[MAX_PATH] = {};
    GetModuleFileNameW(module, full, MAX_PATH);
    LogFileVersion(full);
    log::Line("real driver base=%p", static_cast<void*>(module));

    using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    const auto getClassObject = reinterpret_cast<GetClassObjectFn>(GetProcAddress(module, "DllGetClassObject"));
    if (!getClassObject) {
        log::Line("real driver has no DllGetClassObject export");
        return nullptr;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(CLSID_Stadium, IID_IClassFactory, reinterpret_cast<void**>(&factory));
    log::Line("real DllGetClassObject -> 0x%08lX", hr);
    if (FAILED(hr) || !factory) return nullptr;

    IASIO* driver = nullptr;
    hr = factory->CreateInstance(nullptr, CLSID_Stadium, reinterpret_cast<void**>(&driver));
    factory->Release();
    log::Line("real IClassFactory::CreateInstance -> 0x%08lX object=%p", hr, static_cast<void*>(driver));
    if (FAILED(hr)) return nullptr;
    return driver;
}

namespace {

template <class F>
void ForEachThread(F&& f) {
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 te = {sizeof te};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID == pid) f(te.th32ThreadID);
    }
    CloseHandle(snap);
}

}  // namespace

void DescribeThreadStart(DWORD tid, char* out, size_t cap) {
    using QueryFn = NTSTATUS(NTAPI*)(HANDLE, int, PVOID, ULONG, PULONG);
    static const auto query =
        reinterpret_cast<QueryFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
    strncpy_s(out, cap, "?", _TRUNCATE);
    const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!thread || !query) return;
    PVOID start = nullptr;
    const NTSTATUS st = query(thread, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof start, nullptr);
    CloseHandle(thread);
    if (st < 0 || !start) return;
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(start), &mod)) {
        snprintf(out, cap, "%p (no module)", start);
        return;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(mod, path, MAX_PATH);
    const wchar_t* base = wcsrchr(path, L'\\');
    char baseA[MAX_PATH * 3];
    log::Utf8(base ? base + 1 : path, baseA, sizeof baseA);
    snprintf(out, cap, "%s+0x%llX", baseA, reinterpret_cast<uintptr_t>(start) - reinterpret_cast<uintptr_t>(mod));
}

void SnapshotThreads(ThreadSnapshot& out) {
    out.count = 0;
    ForEachThread([&](DWORD tid) {
        if (out.count < static_cast<int>(sizeof out.ids / sizeof out.ids[0])) out.ids[out.count++] = tid;
    });
}

void LogNewThreads(const ThreadSnapshot& baseline, const char* when) {
    int total = 0, fresh = 0;
    ForEachThread([&](DWORD tid) {
        ++total;
        for (int i = 0; i < baseline.count; ++i) {
            if (baseline.ids[i] == tid) return;
        }
        ++fresh;
        char start[MAX_PATH * 3];
        DescribeThreadStart(tid, start, sizeof start);
        log::Line("  new thread %lu, start %s", tid, start);
    });
    log::Line("threads %s: %d total, %d created since the proxy object was created", when, total, fresh);
}

}  // namespace cb
