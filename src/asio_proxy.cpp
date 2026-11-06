#include "asio_proxy.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "callback_shim.h"
#include "constants.h"
#include "logger.h"
#include "module.h"
#include "real_driver.h"

namespace cb {
namespace {

bool Sample(LONG& counter, LONG& value) {
    value = InterlockedIncrement(&counter);
    return value <= 8 || value % 1000 == 0;
}

const char* ApartmentName(HRESULT hr, APTTYPE type) {
    if (FAILED(hr)) return "COM not initialized on this thread";
    switch (type) {
        case APTTYPE_STA: return "STA";
        case APTTYPE_MTA: return "MTA";
        case APTTYPE_NA: return "NA";
        case APTTYPE_MAINSTA: return "MAINSTA";
        default: return "?";
    }
}

}  // namespace

AsioProxy::AsioProxy() {
    ModuleLock();
    SnapshotThreads(threadsAtCreate_);
    log::Line("AsioProxy %p created (%d threads in process)", static_cast<void*>(this), threadsAtCreate_.count);
}

AsioProxy::~AsioProxy() {
    log::Line("AsioProxy %p destroying (faulted=%d)", static_cast<void*>(this), faulted_);
    shim::Release(this);
    if (real_) {
        Call("Release", [&] { real_->Release(); });
        real_ = nullptr;
        LogNewThreads(threadsAtCreate_, "after Release");
    }
    if (module_) {
        // Never FreeLibrary the driver. 2026-10-27: Rocksmith+ enumerates ASIO drivers at launch
        // (init, probe, Release) and died right after the proxy unloaded the driver DLL on its
        // S_OK from DllCanUnloadNow. A long-lived STA host like Ableton never unloads it. We only
        // report what the driver claims.
        using CanUnloadFn = HRESULT(STDAPICALLTYPE*)();
        const auto canUnload = reinterpret_cast<CanUnloadFn>(GetProcAddress(module_, "DllCanUnloadNow"));
        const HRESULT hr = canUnload ? canUnload() : S_FALSE;
        log::Line("real DllCanUnloadNow -> 0x%08lX; driver kept loaded", hr);
        module_ = nullptr;
    }
    ModuleUnlock();
}

// ---- guards -------------------------------------------------------------------------

template <class F>
bool AsioProxy::Call(const char* what, F&& f) {
    if (Guarded(what, &fault_, f)) return true;
    faulted_ = true;
    char modA[MAX_PATH * 3];
    log::Utf8(fault_.module, modA, sizeof modA);
    const char* base = strrchr(modA, '\\');
    base = base ? base + 1 : modA;
    snprintf(error_, sizeof error_, "CrashBarrier: 0x%08lX in %s at %s+0x%llX", fault_.code, what, base,
             fault_.offset);
    log::Line("proxy is now faulted; host sees: '%s'", error_);
    return false;
}

bool AsioProxy::Ready(const char* what) {
    if (!faulted_ && real_) return true;
    LONG n;  // sampled: a host may keep polling the hot path after a fault
    if (Sample(refusals_, n)) {
        log::Line("%s refused: %s (refusal #%ld)", what, faulted_ ? "proxy is faulted" : "init not called", n);
    }
    return false;
}

template <class F>
ASIOError AsioProxy::Fwd(const char* what, F&& f) {
    if (!Ready(what)) return ASE_NotPresent;
    ASIOError r = ASE_NotPresent;
    if (!Call(what, [&] { r = f(); })) return ASE_NotPresent;
    return r;
}

// ---- IUnknown -----------------------------------------------------------------------

HRESULT AsioProxy::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    // ASIO: the CLSID doubles as the IID. In stealth mode the host asks for the Stadium's.
    if (riid == IID_IUnknown || riid == CLSID_CrashBarrier || (Stealth() && riid == CLSID_Stadium)) {
        *ppv = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    char iid[40];
    log::Guid(riid, iid, sizeof iid);
    log::Line("QueryInterface(%s) -> E_NOINTERFACE", iid);
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG AsioProxy::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

ULONG AsioProxy::Release() {
    const LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return static_cast<ULONG>(n);
}

// ---- init & identity ----------------------------------------------------------------

void AsioProxy::LogInitContext(void* sysRef) {
    const DWORD tid = GetCurrentThreadId();
    char desc[128] = "(none)";
    wchar_t* descW = nullptr;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &descW)) && descW) {
        if (*descW) log::Utf8(descW, desc, sizeof desc);
        LocalFree(descW);
    }
    APTTYPE apt = APTTYPE_CURRENT;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    const HRESULT hr = CoGetApartmentType(&apt, &qualifier);
    log::Line("init context: tid=%lu desc='%s' guiThread=%d priority=%d apartment=%s (hr=0x%08lX type=%d "
              "qualifier=%d)",
              tid, desc, IsGUIThread(FALSE) != 0, GetThreadPriority(GetCurrentThread()), ApartmentName(hr, apt),
              hr, static_cast<int>(apt), static_cast<int>(qualifier));

    const HWND wnd = static_cast<HWND>(sysRef);
    if (!sysRef) {
        log::Line("init sysRef=NULL");
    } else if (!IsWindow(wnd)) {
        log::Line("init sysRef=%p is NOT a window handle", sysRef);
    } else {
        DWORD wndPid = 0;
        const DWORD wndTid = GetWindowThreadProcessId(wnd, &wndPid);
        wchar_t clsW[64] = {}, titleW[64] = {};
        GetClassNameW(wnd, clsW, 64);
        GetWindowTextW(wnd, titleW, 64);
        char cls[192], title[192];
        log::Utf8(clsW, cls, sizeof cls);
        log::Utf8(titleW, title, sizeof title);
        log::Line("init sysRef=%p is a window: class='%s' title='%s' visible=%d ownerTid=%lu (%s) ownerPid=%lu (%s)",
                  sysRef, cls, title, IsWindowVisible(wnd) != 0, wndTid,
                  wndTid == tid ? "calling thread" : "OTHER THREAD", wndPid,
                  wndPid == GetCurrentProcessId() ? "this process" : "OTHER PROCESS");
    }
}

// Read-only forensics on the v0.0.3.15 driver object, layout from the fault dumps (docs/DIAGNOSIS.md):
// channel counts at +0x2C0/+0x2C4, the count the streaming thread indexes with at +0x2C8.
void AsioProxy::LogDriverFields(const char* when) {
    if (!real_ || faulted_) return;
    const auto* p = reinterpret_cast<const unsigned char*>(real_);
    unsigned long v[6] = {};
    FaultRecord scratch;
    if (Guarded("peek", &scratch, [&] {
            for (int i = 0; i < 6; ++i) v[i] = *reinterpret_cast<const unsigned long*>(p + 0x2C0 + 4 * i);
        })) {
        log::Line("driver fields %s: +2C0=%lu +2C4=%lu +2C8=%lu +2CC=%lu +2D0=%lu +2D4=%lu", when, v[0], v[1],
                  v[2], v[3], v[4], v[5]);
    }
}

ASIOBool AsioProxy::init(void* sysHandle) {
    log::Line("init(sysRef=%p)", sysHandle);
    LogInitContext(sysHandle);
    if (faulted_) {
        log::Line("init refused: proxy is faulted");
        return ASIOFalse;
    }
    if (!real_) {
        if (!Call("LoadRealDriver", [&] { real_ = LoadRealDriver(module_); })) return ASIOFalse;
        if (!real_) {
            strncpy_s(error_, "CrashBarrier: could not load the Line 6 driver (see trace)", _TRUNCATE);
            return ASIOFalse;
        }
    }

    ASIOBool ok = ASIOFalse;
    if (!Call("init", [&] { ok = real_->init(sysHandle); })) return ASIOFalse;

    char name[64] = {};
    long version = 0;
    if (!Call("getDriverName", [&] { real_->getDriverName(name); })) return ASIOFalse;
    if (!Call("getDriverVersion", [&] { version = real_->getDriverVersion(); })) return ASIOFalse;
    log::Line("init -> %ld  real driver name='%s' version=%ld", ok, name, version);
    LogNewThreads(threadsAtCreate_, "after init");
    LogDriverFields("after init");
    if (ok != ASIOTrue) {
        char msg[256] = {};
        if (Call("getErrorMessage", [&] { real_->getErrorMessage(msg); })) {
            log::Line("real driver error message: '%s'", msg);
        }
    }
    return ok;
}

void AsioProxy::getDriverName(char* name) {
    if (!name) return;
    if (!Stealth()) {
        strcpy_s(name, 32, kProxyNameA);
    } else {
        char real[64] = {};
        if (real_ && !faulted_ && Call("getDriverName", [&] { real_->getDriverName(real); }) && real[0]) {
            strncpy_s(name, 32, real, _TRUNCATE);
        } else {
            strcpy_s(name, 32, kRealDriverNameA);  // before init: what the real driver reports
        }
    }
    log::Line("getDriverName -> '%s'", name);
}

long AsioProxy::getDriverVersion() {
    long v = 0;
    if (Ready("getDriverVersion")) Call("getDriverVersion", [&] { v = real_->getDriverVersion(); });
    log::Line("getDriverVersion -> %ld", v);
    return v;
}

void AsioProxy::getErrorMessage(char* string) {
    if (!string) return;
    if (faulted_) {
        strncpy_s(string, 124, error_, _TRUNCATE);
    } else if (!real_) {
        strncpy_s(string, 124, error_[0] ? error_ : "CrashBarrier: driver not initialized", _TRUNCATE);
    } else {
        char msg[256] = {};
        if (Call("getErrorMessage", [&] { real_->getErrorMessage(msg); })) {
            strncpy_s(string, 124, msg, _TRUNCATE);
        } else {
            strncpy_s(string, 124, error_, _TRUNCATE);
        }
    }
    log::Line("getErrorMessage -> '%s'", string);
}

// ---- control path -------------------------------------------------------------------

ASIOError AsioProxy::start() {
    LogDriverFields("before start");
    const ASIOError r = Fwd("start", [&] { return real_->start(); });
    log::Line("start -> %ld", r);
    LogDriverFields("after start");
    return r;
}

ASIOError AsioProxy::stop() {
    LogDriverFields("before stop");
    const ASIOError r = Fwd("stop", [&] { return real_->stop(); });
    log::Line("stop -> %ld", r);
    return r;
}

ASIOError AsioProxy::getChannels(long* in, long* out) {
    const ASIOError r = Fwd("getChannels", [&] { return real_->getChannels(in, out); });
    log::Line("getChannels -> %ld (in=%ld out=%ld)", r, in ? *in : -1L, out ? *out : -1L);
    return r;
}

ASIOError AsioProxy::getLatencies(long* in, long* out) {
    const ASIOError r = Fwd("getLatencies", [&] { return real_->getLatencies(in, out); });
    log::Line("getLatencies -> %ld (in=%ld out=%ld)", r, in ? *in : -1L, out ? *out : -1L);
    return r;
}

ASIOError AsioProxy::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) {
    const ASIOError r =
        Fwd("getBufferSize", [&] { return real_->getBufferSize(minSize, maxSize, preferredSize, granularity); });
    log::Line("getBufferSize -> %ld (min=%ld max=%ld preferred=%ld granularity=%ld)", r, minSize ? *minSize : -1L,
              maxSize ? *maxSize : -1L, preferredSize ? *preferredSize : -1L, granularity ? *granularity : -1L);
    return r;
}

ASIOError AsioProxy::canSampleRate(ASIOSampleRate rate) {
    const ASIOError r = Fwd("canSampleRate", [&] { return real_->canSampleRate(rate); });
    log::Line("canSampleRate(%.1f) -> %ld", rate, r);
    return r;
}

ASIOError AsioProxy::getSampleRate(ASIOSampleRate* rate) {
    const ASIOError r = Fwd("getSampleRate", [&] { return real_->getSampleRate(rate); });
    log::Line("getSampleRate -> %ld (%.1f)", r, rate ? *rate : -1.0);
    return r;
}

ASIOError AsioProxy::setSampleRate(ASIOSampleRate rate) {
    const ASIOError r = Fwd("setSampleRate", [&] { return real_->setSampleRate(rate); });
    log::Line("setSampleRate(%.1f) -> %ld", rate, r);
    return r;
}

ASIOError AsioProxy::getClockSources(ASIOClockSource* clocks, long* numSources) {
    const long requested = numSources ? *numSources : -1L;
    const ASIOError r = Fwd("getClockSources", [&] { return real_->getClockSources(clocks, numSources); });
    log::Line("getClockSources(capacity=%ld) -> %ld (num=%ld first='%.32s')", requested, r,
              numSources ? *numSources : -1L,
              (r == ASE_OK && clocks && numSources && *numSources > 0) ? clocks[0].name : "");
    return r;
}

ASIOError AsioProxy::setClockSource(long reference) {
    const ASIOError r = Fwd("setClockSource", [&] { return real_->setClockSource(reference); });
    log::Line("setClockSource(%ld) -> %ld", reference, r);
    return r;
}

ASIOError AsioProxy::getChannelInfo(ASIOChannelInfo* info) {
    if (!info) return ASE_InvalidParameter;
    const ASIOError r = Fwd("getChannelInfo", [&] { return real_->getChannelInfo(info); });
    if (r == ASE_OK) {
        log::Line("getChannelInfo(channel=%ld isInput=%ld) -> %ld (name='%.32s' type=%ld active=%ld group=%ld)",
                  info->channel, info->isInput, r, info->name, info->type, info->isActive, info->channelGroup);
    } else {
        log::Line("getChannelInfo(channel=%ld isInput=%ld) -> %ld", info->channel, info->isInput, r);
    }
    return r;
}

ASIOError AsioProxy::createBuffers(ASIOBufferInfo* infos, long numChannels, long bufferSize,
                                   ASIOCallbacks* callbacks) {
    log::Line("createBuffers(numChannels=%ld bufferSize=%ld callbacks=%p)", numChannels, bufferSize,
              static_cast<void*>(callbacks));
    if (!infos || !callbacks || numChannels <= 0) return ASE_InvalidParameter;
    for (long i = 0; i < numChannels; ++i) {
        log::Line("  buffer[%ld]: %s channel %ld", i, infos[i].isInput ? "input " : "output", infos[i].channelNum);
    }
    ASIOCallbacks* shimmed = shim::Acquire(this, callbacks);
    if (!shimmed) return ASE_NoMemory;

    // Remediation for driver v0.0.3.15 (docs/DIAGNOSIS.md): with zero output channels in the set,
    // the driver's stream thread indexes its output table with outputs-1 and faults ~10 ms after
    // start(). Rocksmith+ opens the guitar input as an input-only set. Give the driver one hidden
    // output channel; the host never sees it and its buffers stay silent (driver-allocated, zeroed).
    bool hasOutput = false;
    for (long i = 0; i < numChannels; ++i) hasOutput = hasOutput || !infos[i].isInput;
    std::vector<ASIOBufferInfo> padded;
    ASIOBufferInfo* driverInfos = infos;
    long driverCount = numChannels;
    if (!hasOutput) {
        padded.assign(infos, infos + numChannels);
        padded.push_back({ASIOFalse, 0, {nullptr, nullptr}});
        driverInfos = padded.data();
        driverCount = numChannels + 1;
        log::Line("  shim: input-only set, adding hidden output channel 0 for the driver");
    }

    const ASIOError r = Fwd("createBuffers",
                            [&] { return real_->createBuffers(driverInfos, driverCount, bufferSize, shimmed); });
    if (r != ASE_OK) {
        shim::Release(this);
    } else {
        if (!hasOutput) std::copy_n(padded.begin(), numChannels, infos);  // hand the host its buffers
        for (long i = 0; i < driverCount; ++i) {
            log::Line("  buffer[%ld]: %p / %p%s", i, driverInfos[i].buffers[0], driverInfos[i].buffers[1],
                      i >= numChannels ? "  (hidden)" : "");
        }
    }
    log::Line("createBuffers -> %ld", r);
    LogDriverFields("after createBuffers");
    return r;
}

ASIOError AsioProxy::disposeBuffers() {
    const ASIOError r = Fwd("disposeBuffers", [&] { return real_->disposeBuffers(); });
    shim::Release(this);
    log::Line("disposeBuffers -> %ld", r);
    return r;
}

ASIOError AsioProxy::controlPanel() {
    const ASIOError r = Fwd("controlPanel", [&] { return real_->controlPanel(); });
    log::Line("controlPanel -> %ld", r);
    return r;
}

ASIOError AsioProxy::future(long selector, void* opt) {
    const ASIOError r = Fwd("future", [&] { return real_->future(selector, opt); });
    log::Line("future(selector=%ld/0x%lX opt=%p) -> %ld", selector, selector, opt, r);
    return r;
}

// ---- hot path (sampled logging) -----------------------------------------------------

ASIOError AsioProxy::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) {
    const ASIOError r = Fwd("getSamplePosition", [&] { return real_->getSamplePosition(sPos, tStamp); });
    LONG n;
    if (Sample(samplePositionCalls_, n)) {
        log::Line("getSamplePosition #%ld -> %ld (pos=%lld ts=%lld)", n, r, sPos ? sPos->value() : -1LL,
                  tStamp ? tStamp->value() : -1LL);
    }
    return r;
}

ASIOError AsioProxy::outputReady() {
    const ASIOError r = Fwd("outputReady", [&] { return real_->outputReady(); });
    LONG n;
    if (Sample(outputReadyCalls_, n)) log::Line("outputReady #%ld -> %ld", n, r);
    return r;
}

}  // namespace cb
