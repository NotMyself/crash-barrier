#include "class_factory.h"

#include <objbase.h>

#include <new>

#include "asio_proxy.h"
#include "logger.h"
#include "module.h"

namespace cb {
namespace {

class ClassFactory final : public IClassFactory {
public:
    ClassFactory() { ModuleLock(); }
    ~ClassFactory() { ModuleUnlock(); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        char iid[40];
        log::Guid(riid, iid, sizeof iid);
        if (outer) {
            log::Line("IClassFactory::CreateInstance(iid=%s) refused: aggregation requested", iid);
            return CLASS_E_NOAGGREGATION;
        }
        AsioProxy* proxy = new (std::nothrow) AsioProxy();
        if (!proxy) return E_OUTOFMEMORY;
        const HRESULT hr = proxy->QueryInterface(riid, ppv);
        proxy->Release();
        log::Line("IClassFactory::CreateInstance(iid=%s) -> 0x%08lX", iid, hr);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ModuleLock();
        else ModuleUnlock();
        return S_OK;
    }

private:
    LONG refs_ = 1;
};

}  // namespace

HRESULT CreateClassFactory(REFIID riid, void** ppv) {
    ClassFactory* factory = new (std::nothrow) ClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

}  // namespace cb
