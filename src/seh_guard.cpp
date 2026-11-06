#include "seh_guard.h"

#include <dbghelp.h>
#include <delayimp.h>

#include <cstdio>

#include "logger.h"

namespace cb {
namespace {

constexpr LONG kMaxDumps = 3;
LONG g_dumps = 0;

const char* Describe(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_BREAKPOINT: return "breakpoint";
        case 0xE06D7363: return "unhandled C++ exception";
        case VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND): return "delay-load: DLL not found";
        case VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND): return "delay-load: export not found";
        default: return "exception";
    }
}

void WriteDump(EXCEPTION_POINTERS* ep, FaultRecord* out) {
    out->dump[0] = 0;
    if (InterlockedIncrement(&g_dumps) > kMaxDumps) return;
    const wchar_t* dir = log::Dir();
    if (!dir[0]) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf_s(out->dump, L"%s\\fault_%lu_%04u%02u%02u-%02u%02u%02u.dmp", dir, GetCurrentProcessId(), st.wYear,
               st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE h = CreateFileW(out->dump, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        log::Line("minidump: CreateFileW failed, error %lu", GetLastError());
        out->dump[0] = 0;
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION mei = {GetCurrentThreadId(), ep, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithDataSegs |
                                                 MiniDumpWithIndirectlyReferencedMemory);
    if (!MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, &mei, nullptr, nullptr)) {
        log::Line("minidump: MiniDumpWriteDump failed, hr=0x%08lX", GetLastError());
        out->dump[0] = 0;
    }
    CloseHandle(h);
}

thread_local int t_guardDepth = 0;

}  // namespace

bool InsideGuard() { return t_guardDepth > 0; }

void ReportException(EXCEPTION_POINTERS* ep, const char* what, FaultRecord* out) {
    const EXCEPTION_RECORD& er = *ep->ExceptionRecord;
    out->code = er.ExceptionCode;
    out->address = er.ExceptionAddress;
    out->module[0] = 0;
    out->offset = 0;

    HMODULE mod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(er.ExceptionAddress), &mod)) {
        GetModuleFileNameW(mod, out->module, MAX_PATH);
        out->offset = reinterpret_cast<uintptr_t>(er.ExceptionAddress) - reinterpret_cast<uintptr_t>(mod);
    }
    char modA[MAX_PATH * 3];
    log::Utf8(out->module[0] ? out->module : L"<no module>", modA, sizeof modA);

    log::Line("FAULT in %s: %s code=0x%08lX at %p = %s+0x%llX", what, Describe(er.ExceptionCode), er.ExceptionCode,
              er.ExceptionAddress, modA, out->offset);

    if (er.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er.NumberParameters >= 2) {
        const ULONG_PTR kind = er.ExceptionInformation[0];
        log::Line("FAULT detail: %s address %p", kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
                  reinterpret_cast<void*>(er.ExceptionInformation[1]));
    } else if ((er.ExceptionCode == VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND) ||
                er.ExceptionCode == VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND)) &&
               er.NumberParameters >= 1) {
        const auto* dli = reinterpret_cast<const DelayLoadInfo*>(er.ExceptionInformation[0]);
        log::Line("FAULT detail: delay-load of '%s' proc '%s' lastError=%lu", dli->szDll ? dli->szDll : "?",
                  dli->dlp.fImportByName && dli->dlp.szProcName ? dli->dlp.szProcName : "(by ordinal)",
                  dli->dwLastError);
    }

    WriteDump(ep, out);
    if (out->dump[0]) {
        char dumpA[MAX_PATH * 3];
        log::Utf8(out->dump, dumpA, sizeof dumpA);
        log::Line("FAULT minidump: %s", dumpA);
    }
}

namespace {

LONG Filter(EXCEPTION_POINTERS* ep, const char* what, FaultRecord* out) {
    ReportException(ep, what, out);
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace

// No C++ objects with destructors may live in this frame.
bool SehCall(void (*thunk)(void*), void* ctx, const char* what, FaultRecord* out) {
    ++t_guardDepth;
    __try {
        thunk(ctx);
        --t_guardDepth;
        return true;
    } __except (Filter(GetExceptionInformation(), what, out)) {
        --t_guardDepth;
        return false;
    }
}

}  // namespace cb
