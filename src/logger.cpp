#include "logger.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

#include "constants.h"

namespace cb::log {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
HANDLE g_file = INVALID_HANDLE_VALUE;
bool g_failed = false;
wchar_t g_dir[MAX_PATH] = {};

void WriteRaw(const char* s, size_t n) {
    DWORD written = 0;
    WriteFile(g_file, s, static_cast<DWORD>(n), &written, nullptr);
    FlushFileBuffers(g_file);
}

void Printf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) WriteRaw(buf, static_cast<size_t>(n) < sizeof buf ? static_cast<size_t>(n) : sizeof buf - 1);
}

void WriteHeader() {
    wchar_t image[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, image, MAX_PATH);
    char imageA[MAX_PATH * 3];
    Utf8(image, imageA, sizeof imageA);

    HMODULE self = nullptr;
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&WriteHeader), &self);
    GetModuleFileNameW(self, selfPath, MAX_PATH);
    char selfA[MAX_PATH * 3];
    Utf8(selfPath, selfA, sizeof selfA);

    RTL_OSVERSIONINFOW os = {sizeof os};
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) fn(&os);
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    Printf("# CrashBarrier %s trace\r\n", CRASHBARRIER_VERSION);
    Printf("# started   %04u-%02u-%02u %02u:%02u:%02u\r\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
           st.wSecond);
    Printf("# process   %s\r\n", imageA);
    Printf("# pid       %lu\r\n", GetCurrentProcessId());
    Printf("# os        %lu.%lu build %lu\r\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber);
    Printf("# proxy dll %s\r\n", selfA);
}

// Caller holds g_lock.
void Open() {
    if (g_file != INVALID_HANDLE_VALUE || g_failed) return;

    wchar_t local[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        g_failed = true;
        return;
    }
    swprintf_s(g_dir, L"%s\\%s", local, kLogDirName);
    if (!CreateDirectoryW(g_dir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        g_failed = true;
        g_dir[0] = 0;
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\trace_%lu_%04u%02u%02u-%02u%02u%02u.log", g_dir, GetCurrentProcessId(), st.wYear,
               st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
        g_failed = true;
        return;
    }
    WriteHeader();
}

}  // namespace

void Line(const char* fmt, ...) {
    char buf[1536];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = snprintf(buf, sizeof buf, "%02u:%02u:%02u.%03u [%5lu] ", st.wHour, st.wMinute, st.wSecond,
                     st.wMilliseconds, GetCurrentThreadId());
    if (n < 0) n = 0;

    va_list ap;
    va_start(ap, fmt);
    const size_t cap = sizeof buf - static_cast<size_t>(n) - 2;  // room for CRLF
    int m = vsnprintf(buf + n, cap, fmt, ap);
    va_end(ap);
    if (m < 0) m = 0;
    size_t len = static_cast<size_t>(n) + (static_cast<size_t>(m) < cap ? static_cast<size_t>(m) : cap - 1);
    buf[len++] = '\r';
    buf[len++] = '\n';

    AcquireSRWLockExclusive(&g_lock);
    Open();
    if (g_file != INVALID_HANDLE_VALUE) WriteRaw(buf, len);
    ReleaseSRWLockExclusive(&g_lock);
}

const wchar_t* Dir() {
    AcquireSRWLockExclusive(&g_lock);
    Open();
    ReleaseSRWLockExclusive(&g_lock);
    return g_dir;
}

void Utf8(const wchar_t* in, char* out, size_t cap) {
    if (cap == 0) return;
    out[0] = 0;
    if (!in) return;
    if (WideCharToMultiByte(CP_UTF8, 0, in, -1, out, static_cast<int>(cap), nullptr, nullptr) <= 0) {
        strncpy_s(out, cap, "?", _TRUNCATE);
    }
}

void Guid(const GUID& g, char* out, size_t cap) {
    snprintf(out, cap, "{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}", g.Data1, g.Data2,
             g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
}

}  // namespace cb::log
