// The single SEH boundary of the project. Every call into the real driver and into
// the host's callbacks goes through SehCall(). A caught exception is logged with
// module+offset, a minidump is written (first few only), and the call reports failure.
// Nothing is ever rethrown: the hosting process survives.
#pragma once

#include <windows.h>

#include <type_traits>

namespace cb {

struct FaultRecord {
    DWORD code = 0;
    void* address = nullptr;
    wchar_t module[MAX_PATH] = {};
    unsigned long long offset = 0;
    wchar_t dump[MAX_PATH] = {};
};

// Runs thunk(ctx). Returns true on normal completion, false if an exception was caught.
bool SehCall(void (*thunk)(void*), void* ctx, const char* what, FaultRecord* out);

// Fills 'out', logs the exception (module+offset, access detail) tagged with 'what', and writes
// a minidump for the first few. Shared by the SEH filter and the vectored handler.
void ReportException(EXCEPTION_POINTERS* ep, const char* what, FaultRecord* out);

// True while the current thread is inside a SehCall (so the vectored handler can stay quiet and
// let the filter report).
bool InsideGuard();

template <class F>
void Thunk(void* p) {
    (*static_cast<F*>(p))();
}

// Guarded("init", &rec, [&] { r = real->init(h); })
template <class F>
bool Guarded(const char* what, FaultRecord* out, F&& f) {
    return SehCall(&Thunk<std::remove_reference_t<F>>, &f, what, out);
}

}  // namespace cb
