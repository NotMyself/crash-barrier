// FaultyDriver_x64.dll - a fake ASIO driver whose getChannels() reads address 0x10.
// Used to prove the proxy's SEH guard, minidump and faulted-state behaviour without hardware:
//   asiohost_test --dll CrashBarrier_x64.dll --driver FaultyDriver_x64.dll
#include <windows.h>

#include <objbase.h>

#include <cstring>
#include <new>

#include "asio/iasio.h"

namespace {

LONG g_objects = 0;

class FaultyDriver final : public IASIO {
public:
    FaultyDriver() { InterlockedIncrement(&g_objects); }
    ~FaultyDriver() { InterlockedDecrement(&g_objects); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {  // any IID: ASIO uses the CLSID
        *ppv = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }

    ASIOBool init(void*) override { return ASIOTrue; }
    void getDriverName(char* name) override { strcpy_s(name, 32, "Faulty Driver"); }
    long getDriverVersion() override { return 1; }
    void getErrorMessage(char* s) override { strcpy_s(s, 124, "faulty driver: no error recorded"); }
    ASIOError start() override { return ASE_OK; }
    ASIOError stop() override { return ASE_OK; }
    ASIOError getChannels(long* in, long*) override {
        *in = *reinterpret_cast<volatile long*>(static_cast<uintptr_t>(0x10));  // deliberate access violation
        return ASE_OK;
    }
    ASIOError getLatencies(long* in, long* out) override { *in = *out = 64; return ASE_OK; }
    ASIOError getBufferSize(long* mn, long* mx, long* pref, long* gran) override {
        *mn = *mx = *pref = 64;
        *gran = 0;
        return ASE_OK;
    }
    ASIOError canSampleRate(ASIOSampleRate) override { return ASE_OK; }
    ASIOError getSampleRate(ASIOSampleRate* r) override { *r = 48000.0; return ASE_OK; }
    ASIOError setSampleRate(ASIOSampleRate) override { return ASE_OK; }
    ASIOError getClockSources(ASIOClockSource*, long* n) override { *n = 0; return ASE_OK; }
    ASIOError setClockSource(long) override { return ASE_OK; }
    ASIOError getSamplePosition(ASIOSamples* p, ASIOTimeStamp* t) override { *p = *t = {}; return ASE_OK; }
    ASIOError getChannelInfo(ASIOChannelInfo*) override { return ASE_NotPresent; }
    ASIOError createBuffers(ASIOBufferInfo*, long, long, ASIOCallbacks*) override { return ASE_NotPresent; }
    ASIOError disposeBuffers() override { return ASE_OK; }
    ASIOError controlPanel() override { return ASE_OK; }
    ASIOError future(long, void*) override { return ASE_NotPresent; }
    ASIOError outputReady() override { return ASE_OK; }

private:
    LONG refs_ = 1;
};

class Factory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*, REFIID riid, void** ppv) override {
        auto* d = new (std::nothrow) FaultyDriver();
        if (!d) return E_OUTOFMEMORY;
        const HRESULT hr = d->QueryInterface(riid, ppv);
        d->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }

private:
    LONG refs_ = 1;
};

}  // namespace

#pragma comment(linker, "/EXPORT:DllGetClassObject,PRIVATE")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow,PRIVATE")

STDAPI DllGetClassObject(REFCLSID, REFIID riid, void** ppv) {
    auto* f = new (std::nothrow) Factory();
    if (!f) return E_OUTOFMEMORY;
    const HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() { return g_objects == 0 ? S_OK : S_FALSE; }
