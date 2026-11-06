#include "veh.h"

#include <windows.h>

#include <cstdio>

#include "logger.h"
#include "real_driver.h"
#include "seh_guard.h"

namespace cb::veh {
namespace {

constexpr LONG kMaxReports = 50;
LONG g_reports = 0;
LONG g_installed = 0;
FaultRecord g_record;  // reports are serialized by the logger; the record is scratch

bool Interesting(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
           code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_ILLEGAL_INSTRUCTION ||
           code == EXCEPTION_PRIV_INSTRUCTION;
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
    if (!Interesting(ep->ExceptionRecord->ExceptionCode) || InsideGuard()) return EXCEPTION_CONTINUE_SEARCH;
    const LONG n = InterlockedIncrement(&g_reports);
    if (n > kMaxReports) return EXCEPTION_CONTINUE_SEARCH;

    const DWORD tid = GetCurrentThreadId();
    char start[MAX_PATH * 3];
    DescribeThreadStart(tid, start, sizeof start);
    char what[MAX_PATH * 3 + 64];
    snprintf(what, sizeof what, "VEH #%ld thread %lu (start %s)", n, tid, start);
    ReportException(ep, what, &g_record);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void Install() {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       reinterpret_cast<LPCWSTR>(&Install), &self);
    const void* h = AddVectoredExceptionHandler(0 /* last */, &Handler);
    log::Line("vectored exception handler %s; proxy DLL pinned", h ? "installed" : "FAILED to install");
}

}  // namespace cb::veh
